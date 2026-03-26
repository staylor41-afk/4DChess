#!/usr/bin/env python3
"""
8D Multi-Player Chess Engine  —  v3.0
======================================
Board and rules exactly matching 8d-chess-v43.html:

  Board: 8^8 hypercube — axes [X, Y, Z, W, V, U, T, S], each 0-7.
  X (dim 0): column along the back rank (0=Rook … 7=Rook).
  Y-S (dims 1-7): corner address — each is 0 or 7 based on bit of player id.

  Player P home corner:
    [0,  (P&1)?7:0,  (P&2)?7:0,  (P&4)?7:0,  (P&8)?7:0,
         (P&16)?7:0, (P&32)?7:0, (P&64)?7:0 ]

  Back rank  (X=0..7 along home corner): R N B Q K B N R
  Pawns: placed one step toward each unique opponent direction,
         along the full X axis.  Each pawn stores its dir vector.

  Move rules (from getLegal()):
    Rook   — slides along exactly 1 axis.
    Bishop — slides along ≥2 axes simultaneously (equal step size per axis).
             Limited to MAX_DIAG_PAIRS pairs (12 for 8D) matching original.
    Queen  — Rook + Bishop.
    Knight — 2 squares in one axis, 1 in another (all axis combos).
    King   — 1 square axial OR diagonal (limited to MAX_K_PAIRS=8 for 8D).
    Pawn   — 1 forward along dir (2 from start), captures perpendicular+forward.

Port: 8766
"""

import json, random, sys, threading, multiprocessing
from http.server import HTTPServer, BaseHTTPRequestHandler
from itertools import product

PORT     = 8766
NDIM     = 8
BSIZE    = 8   # board size per dimension (0 .. 7)
BACK_RANK = ['R','N','B','Q','K','B','N','R']
FULL_NAME = {'R':'Rook','N':'Knight','B':'Bishop','Q':'Queen','K':'King','P':'Pawn'}
MAX_DIAG_PAIRS = 12   # matches original: capped for na>4
MAX_K_PAIRS    = 8    # matches original king diagonal cap for high dims

# Piece values for MVV-LVA scoring and weighted piece selection.
# Major pieces are upweighted ~100-500× relative to Pawns so the AI picks
# them proportionally despite Pawns making up ~99% of the army in 8D.
PIECE_VAL  = {'K': 10000, 'Q': 900, 'R': 500, 'B': 320, 'N': 300, 'P': 100}
# Selection weight (separate from material value so King isn't always moved)
PIECE_SEL_WEIGHT = {'K': 50, 'Q': 400, 'R': 250, 'B': 150, 'N': 150, 'P': 1}

N_WORKERS  = 8    # parallel scoring workers
ENEMY_CHUNK = 16  # enemies per worker

# ── Module-level parallel worker ──────────────────────────────────────────────
# Must be at module level so multiprocessing (spawn mode) can pickle it.
#
# Each worker receives a list of candidate moves and a cluster of ~16 enemy
# coords.  It scores every move against THAT cluster and returns its best pick.
# The main process ("chooser") then picks the best result across all 8 workers.
#
# move_entry format: (frm, to, target_type_or_None, target_pid_or_None, mover_type)

def _score_worker(args):
    moves, enemy_cluster, seed = args
    random.seed(seed)

    def cheb(a, b):
        return max(abs(a[i] - b[i]) for i in range(len(a)))

    best_cap = None   # (score, frm, to, ttype, tpid)
    best_pos = None   # (score, frm, to)

    for (frm, to, ttype, tpid, mtype) in moves:
        if ttype is not None:
            # MVV-LVA: maximise victim, minimise attacker cost
            score = PIECE_VAL.get(ttype, 100) * 10 - PIECE_VAL.get(mtype, 100)
            if best_cap is None or score > best_cap[0]:
                best_cap = (score, frm, to, ttype, tpid)
        else:
            if enemy_cluster:
                before = min(cheb(frm, e) for e in enemy_cluster)
                after  = min(cheb(to,  e) for e in enemy_cluster)
                adv    = before - after
            else:
                adv = 0
            score = adv + random.random() * 0.4
            if best_pos is None or score > best_pos[0]:
                best_pos = (score, frm, to)

    return (best_cap, best_pos)


# ── Process pool (created once per process, reused across turns) ───────────────
_pool = None

def _get_pool():
    global _pool
    if _pool is None:
        try:
            ctx  = multiprocessing.get_context('spawn')
            _pool = ctx.Pool(N_WORKERS)
            print(f'[8d-py] Parallel pool: {N_WORKERS} workers ready', flush=True)
        except Exception as exc:
            print(f'[8d-py] Pool init failed ({exc}) — serial fallback', flush=True)
            _pool = False   # sentinel: don't retry
    return _pool if _pool else None

# ── Piece ─────────────────────────────────────────────────────────────────────

class Piece:
    __slots__ = ['type','pid','coords','dir','orig_pid']
    def __init__(self, t, pid, coords, dir_=None):
        self.type     = t
        self.pid      = pid
        self.coords   = list(coords)
        self.dir      = dir_          # pawn direction vector or None
        self.orig_pid = pid

def ckey(c): return tuple(c)

# ── Game ──────────────────────────────────────────────────────────────────────

class Game:
    def __init__(self, dims, num_players, game_id):
        self.dims        = min(max(dims, 2), NDIM)
        self.num_players = min(max(num_players, 2), 128)
        self.game_id     = game_id
        self.turn_number = 0
        self.alive       = [True] * self.num_players
        self.board       = {}    # ckey → Piece
        self.move_log    = []    # compact move records for /moves endpoint
        self.last_player = 0
        self._setup()

    # ── Corner / home ──────────────────────────────────────────────────────────

    def _home(self, pid):
        """Home corner for player pid: dim0=0, dim i = (pid >> (i-1))&1 * 7."""
        c = [0] * NDIM
        for i in range(1, self.dims):
            c[i] = 7 if (pid >> (i - 1)) & 1 else 0
        return c

    # ── Setup ─────────────────────────────────────────────────────────────────

    def _setup(self):
        for pid in range(self.num_players):
            home = self._home(pid)

            # Back rank: X = 0..7, dims 1+ fixed at home corner
            for x in range(BSIZE):
                c    = home[:]
                c[0] = x
                k    = ckey(c)
                self.board[k] = Piece(BACK_RANK[x], pid, c)

            # Pawns: one row in front of back rank toward each unique opponent
            seen_dirs = set()
            for opp in range(self.num_players):
                if opp == pid:
                    continue
                opp_home = self._home(opp)
                # Direction vector: 0 in dim 0, sign of diff in dims 1+
                dir_ = [0] * NDIM
                for a in range(1, self.dims):
                    d = opp_home[a] - home[a]
                    dir_[a] = 1 if d > 0 else (-1 if d < 0 else 0)
                if all(dir_[a] == 0 for a in range(1, self.dims)):
                    continue     # same corner, skip
                dk = tuple(dir_)
                if dk in seen_dirs:
                    continue
                seen_dirs.add(dk)
                # Place pawn row
                for x in range(BSIZE):
                    c = [home[a] + dir_[a] if a > 0 else x for a in range(NDIM)]
                    if any(c[a] < 0 or c[a] >= BSIZE for a in range(NDIM)):
                        continue
                    k = ckey(c)
                    if k not in self.board:
                        self.board[k] = Piece('P', pid, c, dir_=dir_[:])

    # ── Bounds check ──────────────────────────────────────────────────────────

    def _ok(self, c):
        return all(0 <= v < BSIZE for v in c)

    # ── Move generation — exact replica of getLegal() ─────────────────────────

    def get_legal(self, coords):
        """Return list of (from_coords, to_coords, target_piece_or_None)."""
        k = ckey(coords)
        piece = self.board.get(k)
        if not piece:
            return []

        pid   = piece.pid
        ptype = piece.type
        na    = self.dims
        moves = []

        def at(c):
            if not self._ok(c): return None
            return self.board.get(ckey(c))

        def add(c):
            if not self._ok(c): return
            t = at(c)
            if t and t.pid == pid: return
            moves.append((list(coords), list(c), t))

        def slide(d):
            cur = list(coords)
            for _ in range(BSIZE):
                cur = [cur[i] + d[i] for i in range(NDIM)]
                if not self._ok(cur): break
                t = at(cur)
                if t:
                    if t.pid != pid:
                        moves.append((list(coords), list(cur), t))
                    break
                moves.append((list(coords), list(cur), None))

        if ptype in ('R', 'B', 'Q'):
            # Orthogonal slides (rook / queen)
            if ptype in ('R', 'Q'):
                for a in range(na):
                    for s in (1, -1):
                        d = [0] * NDIM; d[a] = s
                        slide(d)

            # Diagonal slides (bishop / queen) — capped at MAX_DIAG_PAIRS
            if ptype in ('B', 'Q'):
                pairs = 0
                for a in range(na):
                    if pairs >= MAX_DIAG_PAIRS: break
                    for b in range(a + 1, na):
                        if pairs >= MAX_DIAG_PAIRS: break
                        pairs += 1
                        for sa in (1, -1):
                            for sb in (1, -1):
                                d = [0] * NDIM; d[a] = sa; d[b] = sb
                                slide(d)

            # Filter by piece type
            if ptype == 'R':
                moves[:] = [(f,t,cap) for f,t,cap in moves
                            if sum(1 for i in range(NDIM) if t[i] != f[i]) == 1]
            elif ptype == 'B':
                moves[:] = [(f,t,cap) for f,t,cap in moves
                            if sum(1 for i in range(NDIM) if t[i] != f[i]) >= 2]

        elif ptype == 'N':
            for a in range(na):
                for b in range(na):
                    if a == b: continue
                    for la in (2, -2):
                        for lb in (1, -1):
                            c = list(coords)
                            c[a] += la; c[b] += lb
                            add(c)

        elif ptype == 'K':
            # Axial moves
            for a in range(na):
                for s in (-1, 1):
                    c = list(coords); c[a] += s
                    add(c)
            # Diagonal moves — capped at MAX_K_PAIRS
            pairs = 0
            for a in range(na):
                if pairs >= MAX_K_PAIRS: break
                for b in range(a + 1, na):
                    if pairs >= MAX_K_PAIRS: break
                    pairs += 1
                    for sa in (-1, 1):
                        for sb in (-1, 1):
                            c = list(coords)
                            c[a] += sa; c[b] += sb
                            add(c)

        elif ptype == 'P':
            dir_ = piece.dir
            if not dir_:
                return []
            # Forward move
            fwd = [coords[i] + dir_[i] for i in range(NDIM)]
            if self._ok(fwd) and not at(fwd):
                moves.append((list(coords), fwd[:], None))
                # Double push from start row
                start_c = [self._home(piece.orig_pid)[a] + dir_[a] if a > 0 else coords[0]
                            for a in range(NDIM)]
                if ckey(coords) == ckey(start_c):
                    dbl = [fwd[i] + dir_[i] for i in range(NDIM)]
                    if self._ok(dbl) and not at(dbl):
                        moves.append((list(coords), dbl[:], None))
            # Diagonal captures — perpendicular to dir
            for a in range(na):
                if dir_[a]: continue   # skip the forward axis
                for s in (-1, 1):
                    cc = [fwd[i] + (s if i == a else 0) for i in range(NDIM)]
                    if not self._ok(cc): continue
                    t = at(cc)
                    if t and t.pid != pid:
                        moves.append((list(coords), cc[:], t))

        return moves

    # ── AI ────────────────────────────────────────────────────────────────────

    def ai_turn(self, pid):
        """
        Parallel committee AI  —  v3.0
        ================================
        1. Generate candidate moves on the main process (weighted piece
           selection: Queens/Rooks/Bishops heavily favoured over Pawns).

        2. Sort ALL enemies by Chebyshev distance to this player's centroid.
           Split sorted list into N_WORKERS chunks of ENEMY_CHUNK (~16) each:
             worker 0 → nearest 16 enemies
             worker 1 → enemies 17-32
             …
             worker 7 → enemies 113-128 (or furthest available)

        3. Fan out to the multiprocessing pool.  Each worker scores every
           candidate move against ITS enemy cluster and returns its single
           best recommendation (capture or positional).

        4. Chooser: captures beat positional moves; among captures pick
           highest MVV-LVA score; among positional picks highest advancement.
           Apply chosen move to the authoritative board.
        """
        if not self.alive[pid]:
            return None

        my_pieces = [p for p in self.board.values() if p.pid == pid]
        if not my_pieces:
            self.alive[pid] = False
            return None

        # ── Step 1: generate candidate moves (main process, weighted) ─────────
        weights = [PIECE_SEL_WEIGHT.get(p.type, 1) for p in my_pieces]
        total_w = sum(weights)
        MAX_PIECES = min(len(my_pieces), 30)

        moves_data = []   # (frm, to, ttype|None, tpid|None, mover_type)
        seen_keys  = set()
        attempts   = 0

        while len(seen_keys) < MAX_PIECES and attempts < MAX_PIECES * 6:
            attempts += 1
            r = random.random() * total_w
            cumul = 0.0
            chosen = my_pieces[-1]
            for i, p in enumerate(my_pieces):
                cumul += weights[i]
                if r <= cumul:
                    chosen = p
                    break
            ck = ckey(chosen.coords)
            if ck in seen_keys:
                continue
            seen_keys.add(ck)
            for (frm, to, tgt) in self.get_legal(chosen.coords):
                moves_data.append((
                    list(frm), list(to),
                    tgt.type if tgt else None,
                    tgt.pid  if tgt else None,
                    chosen.type,
                ))

        if not moves_data:
            # Full scan fallback
            for p in my_pieces:
                for (frm, to, tgt) in self.get_legal(p.coords):
                    moves_data.append((list(frm), list(to),
                        tgt.type if tgt else None,
                        tgt.pid  if tgt else None,
                        p.type))
            if not moves_data:
                return None

        # ── Step 2: sort enemies by distance to our centroid ──────────────────
        cx = [sum(p.coords[d] for p in my_pieces) / len(my_pieces)
              for d in range(NDIM)]

        def cheb_centroid(coords):
            return max(abs(coords[d] - cx[d]) for d in range(NDIM))

        enemies = sorted(
            (p.coords for p in self.board.values() if p.pid != pid),
            key=cheb_centroid
        )

        # Build N_WORKERS chunks of ENEMY_CHUNK enemy coords each
        chunks = []
        for w in range(N_WORKERS):
            start = w * ENEMY_CHUNK
            chunks.append(enemies[start : start + ENEMY_CHUNK])

        # ── Step 3: fan out to worker pool ────────────────────────────────────
        tasks = [
            (moves_data, chunks[w], random.randint(0, 0x7FFFFFFF))
            for w in range(N_WORKERS)
        ]

        pool = _get_pool()
        try:
            if pool:
                results = pool.map(_score_worker, tasks)
            else:
                results = [_score_worker(t) for t in tasks]
        except Exception as exc:
            print(f'[8d-py] pool.map error ({exc}) — serial fallback', flush=True)
            results = [_score_worker(t) for t in tasks]

        # ── Step 4: chooser ───────────────────────────────────────────────────
        # Captures always beat positional moves.
        # Among captures: highest MVV-LVA score wins.
        # Among positional: highest advancement score wins.
        best_cap_score = None
        best_cap_move  = None
        best_pos_score = None
        best_pos_move  = None

        for (cap, pos) in results:
            if cap is not None:
                sc, frm, to, ttype, tpid = cap
                if best_cap_score is None or sc > best_cap_score:
                    best_cap_score = sc
                    best_cap_move  = (frm, to)
            if pos is not None:
                sc, frm, to = pos
                if best_pos_score is None or sc > best_pos_score:
                    best_pos_score = sc
                    best_pos_move  = (frm, to)

        chosen = best_cap_move or best_pos_move
        if not chosen:
            return None

        frm, to   = chosen
        captured  = self.board.get(ckey(to))
        return self._apply(pid, frm, to, captured)

    def apply_move(self, pid, frm, to):
        if not self.alive[pid]: return None          # dead players can't move
        fk = ckey(frm); tk = ckey(to)
        piece    = self.board.get(fk)
        if not piece or piece.pid != pid: return None
        captured = self.board.get(tk)
        if captured and captured.pid == pid: return None  # no self-capture
        return self._apply(pid, frm, to, captured)

    def _apply(self, pid, frm, to, captured):
        fk, tk = ckey(frm), ckey(to)
        piece = self.board.pop(fk, None)
        if not piece: return None

        transferred_from = None  # pid whose pieces were transferred to `pid`
        if captured:
            self.board.pop(tk, None)
            if captured.type == 'K':
                loser = captured.pid
                # Only eliminate + transfer if loser is genuinely a different player
                # and not already dead (prevents double-transfer edge cases)
                if loser != pid and self.alive[loser]:
                    self.alive[loser] = False
                    transferred_from  = loser
                    # Transfer ALL of the loser's remaining pieces to the capturing player
                    for p in self.board.values():
                        if p.pid == loser:
                            p.pid = pid

        piece.coords = list(to)
        self.board[tk] = piece
        self.turn_number += 1
        self.last_player = pid
        self.move_log.append({
            'f':   list(frm),
            't':   list(to),
            'pt':  piece.type,
            'ct':  captured.type if captured else None,
            'cp':  captured.pid  if captured else None,
            'pid': pid,
        })

        alive_with_pieces = set(p.pid for p in self.board.values() if self.alive[p.pid])
        game_over = len(alive_with_pieces) <= 1
        winner    = list(alive_with_pieces)[0] if game_over and alive_with_pieces else None

        return {
            'move':             {'from': list(frm), 'to': list(to)},
            'piece_type':       FULL_NAME.get(piece.type, piece.type),
            'captured_type':    FULL_NAME.get(captured.type, captured.type) if captured else None,
            'captured_pid':     captured.pid  if captured else None,
            'transferred_from': transferred_from,  # non-None when a king was captured
            'player':           pid,
            'turn_number':      self.turn_number,
            'alive':            self.alive[:],  # full alive snapshot for client sync
            'game_over':        game_over,
            'winner':           winner,
        }

    def state_summary(self, include_pieces=False):
        s = {
            'gameId':      self.game_id,
            'players':     self.num_players,
            'dimensions':  self.dims,
            'turn_number': self.turn_number,
            'alive':       self.alive[:],
            'piece_count': len(self.board),
        }
        if include_pieces:
            s['pieces'] = [
                {'coords': p.coords, 'type': FULL_NAME.get(p.type, p.type), 'pid': p.pid}
                for p in self.board.values()
            ]
        return s

# ── HTTP handler ──────────────────────────────────────────────────────────────

games      = {}
games_lock = threading.Lock()

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_): pass

    def _body(self):
        n = int(self.headers.get('Content-Length', 0))
        return json.loads(self.rfile.read(n)) if n else {}

    def _send(self, data, code=200):
        body = json.dumps(data).encode()
        self.send_response(code)
        self.send_header('Content-Type',   'application/json')
        self.send_header('Content-Length', len(body))
        self.send_header('Access-Control-Allow-Origin',  '*')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(200)
        for h, v in [('Access-Control-Allow-Origin','*'),
                     ('Access-Control-Allow-Methods','GET,POST,OPTIONS'),
                     ('Access-Control-Allow-Headers','Content-Type')]:
            self.send_header(h, v)
        self.end_headers()

    def do_GET(self):
        ep  = self.path.split('?')[0]
        qs  = {}
        if '?' in self.path:
            for part in self.path.split('?', 1)[1].split('&'):
                if '=' in part:
                    k, v = part.split('=', 1)
                    qs[k] = v

        if ep == '/ping':
            self._send({'ok': True, 'engine': '8d-chess-py', 'version': '3.0',
                        'port': PORT, 'max_players': 128,
                        'workers': N_WORKERS, 'enemy_chunk': ENEMY_CHUNK})

        elif ep == '/apply_ai_turn':
            pid = int(qs.get('player', 0))
            gid = str(qs.get('game_id', 'game1'))
            with games_lock:
                g = games.get(gid)
            if not g:
                self._send({'ok': False, 'error': 'game not found'}); return
            result = g.ai_turn(pid)
            if result is None:
                self._send({'ok': True, 'turn': {
                    'pass': True, 'move': None,
                    'piece_type': None, 'captured_type': None}})
                return
            self._send({'ok': True, 'turn': result})

        elif ep == '/legal_moves':
            gid  = str(qs.get('game_id', 'game1'))
            frm_s = qs.get('from', '')
            with games_lock:
                g = games.get(gid)
            if not g or not frm_s:
                self._send({'ok': False, 'error': 'bad request'}); return
            try:
                frm = [int(x) for x in frm_s.split(',')]
            except Exception:
                self._send({'ok': False, 'error': 'bad from coords'}); return
            # Pad/trim to game dims
            frm = (frm + [0]*NDIM)[:NDIM]
            moves = g.get_legal(frm)
            self._send({'ok': True,
                        'moves': [list(to) for (_, to, _) in moves],
                        'count': len(moves)})

        elif ep == '/moves':
            # Lightweight delta feed for the 3-D visualiser.
            # Client sends since=N (index into move_log).
            # Returns moves[N:], new index, turn number, active player.
            since = int(qs.get('since', 0))
            gid   = qs.get('game_id', 'game1')
            with games_lock:
                g = games.get(gid)
            if not g:
                self._send({'ok': False, 'error': 'game not found'}); return
            moves = g.move_log[since:]
            self._send({
                'ok':     True,
                'moves':  moves,
                'idx':    len(g.move_log),   # next since value
                'turn':   g.turn_number,
                'active': g.last_player,
                'pieces': len(g.board),
                'alive':  sum(g.alive),
            })

        else:
            self._send({'ok': False, 'error': 'not found'}, 404)

    def do_POST(self):
        try:
            body = self._body()
            ep   = self.path.split('?')[0]

            if ep == '/ping':
                self._send({'ok': True})

            elif ep == '/new_game':
                dims     = int(body.get('dimensions', body.get('dims', 8)))
                nplayers = int(body.get('players', 2))
                gid      = str(body.get('game_id', 'game1'))
                g = Game(dims, nplayers, gid)
                with games_lock:
                    games[gid] = g
                print(f'[8d-py] new_game  id={gid}  dims={g.dims}  '
                      f'players={g.num_players}  pieces={len(g.board)}', flush=True)
                self._send({'ok': True, 'state': g.state_summary()})

            elif ep == '/apply_ai_turn':
                pid = int(body.get('player', 0))
                gid = str(body.get('game_id', 'game1'))
                with games_lock:
                    g = games.get(gid)
                if not g:
                    self._send({'ok': False, 'error': 'game not found'}); return
                result = g.ai_turn(pid)
                if result is None:
                    self._send({'ok': True, 'turn': {
                        'pass': True, 'move': None,
                        'piece_type': None, 'captured_type': None}})
                    return
                self._send({'ok': True, 'turn': result})

            elif ep == '/apply_move':
                pid = int(body.get('player', 0))
                gid = str(body.get('game_id', 'game1'))
                frm = body.get('from'); to = body.get('to')
                with games_lock:
                    g = games.get(gid)
                if not g or not frm or not to:
                    self._send({'ok': False, 'error': 'bad request'}); return
                result = g.apply_move(pid, frm, to)
                if result is None:
                    self._send({'ok': False, 'error': 'illegal move'})
                else:
                    self._send({'ok': True, 'turn': result})

            elif ep in ('/state', '/get_state', '/board'):
                gid = str(body.get('game_id', 'game1'))
                full = body.get('include_pieces', False)
                with games_lock:
                    g = games.get(gid)
                if not g:
                    self._send({'ok': False, 'error': 'game not found'}); return
                self._send({'ok': True, 'state': g.state_summary(include_pieces=full)})

            else:
                self._send({'ok': False, 'error': f'unknown: {ep}'}, 404)

        except Exception as e:
            import traceback
            traceback.print_exc()
            self._send({'ok': False, 'error': str(e)}, 500)

# ── Entry point ───────────────────────────────────────────────────────────────

if __name__ == '__main__':
    # On Windows multiprocessing requires this guard in the main module.
    multiprocessing.freeze_support()

    port = int(sys.argv[1]) if len(sys.argv) > 1 else PORT
    print(f'[8d-py] 8D Chess Engine v3.0 — port {port}', flush=True)
    print(f'[8d-py] Board: 8^8 hypercube | axes: X Y Z W V U T S | 128 players', flush=True)
    print(f'[8d-py] AI: {N_WORKERS} parallel workers × {ENEMY_CHUNK} enemies each', flush=True)

    # Warm up the process pool before the first request arrives.
    _get_pool()

    server = HTTPServer(('127.0.0.1', port), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('[8d-py] Shutting down.', flush=True)
        if _pool:
            _pool.terminate()
