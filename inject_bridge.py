#!/usr/bin/env python3
"""Inject C++ engine bridge + deep diagnostics into 8d-chess-v43."""
import sys

SRC  = r"C:\Users\Stayl\Downloads\8d-chess-v43 (1).html"
DEST = r"F:\8d chess\8d-chess-v47.0-bridge.html"

with open(SRC, encoding='utf-8') as f:
    html = f.read()

# ── 1. CSS ─────────────────────────────────────────────────────────────────────
BRIDGE_CSS = """
/* ── C++ Bridge Diagnostics Panel ── */
#cppBridgeDiag{position:fixed;bottom:0;right:0;z-index:999;width:280px;background:rgba(6,8,14,.97);border-left:1px solid var(--border);border-top:1px solid var(--border);overflow:hidden;font-family:'Share Tech Mono','Courier New',monospace;font-size:.58rem;box-shadow:-4px 0 20px rgba(0,0,0,.6);display:flex;flex-direction:column;max-height:100vh}
.db-head{display:flex;align-items:center;gap:.25rem;padding:.28rem .45rem;background:rgba(200,168,75,.07);border-bottom:1px solid var(--border);flex-shrink:0}
.db-title{font-family:'Orbitron',sans-serif;font-size:.52rem;font-weight:700;color:var(--gold);letter-spacing:.1em;flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.db-scroll{overflow-y:auto;flex:1;min-height:0}
.db-scroll::-webkit-scrollbar{width:3px}
.db-scroll::-webkit-scrollbar-thumb{background:rgba(62,207,207,.2);border-radius:2px}
.db-sec{font-family:'Orbitron',sans-serif;font-size:.45rem;color:var(--muted);letter-spacing:.1em;padding:.14rem .45rem;background:rgba(20,26,46,.7);border-top:1px solid rgba(26,32,53,.8);border-bottom:1px solid rgba(26,32,53,.8);margin-top:.04rem}
.db-row{display:flex;justify-content:space-between;align-items:center;padding:.1rem .45rem;border-bottom:1px solid rgba(20,26,40,.6);gap:.3rem;min-height:17px}
.db-row:hover{background:rgba(62,207,207,.03)}
.db-lbl{color:#4a5878;font-size:.54rem;letter-spacing:.04em;flex-shrink:0;width:120px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.db-val{color:var(--ink);font-size:.54rem;text-align:right;flex-shrink:0;min-width:60px;white-space:nowrap}
.db-bar{flex:1;height:4px;background:rgba(26,32,53,.8);border-radius:2px;overflow:hidden;margin:0 .3rem;min-width:20px}
.db-bar-fill{height:100%;border-radius:2px;transition:width .3s}
.db-btn{font-family:'Orbitron',sans-serif;font-size:.46rem;padding:.13rem .28rem;border:1px solid var(--border);background:transparent;color:var(--muted);cursor:pointer;border-radius:2px;letter-spacing:.04em;transition:all .1s;white-space:nowrap}
.db-btn:hover{border-color:var(--cyan);color:var(--cyan)}
.db-footer{display:flex;gap:.25rem;padding:.22rem .4rem;border-top:1px solid var(--border);flex-shrink:0;flex-wrap:wrap}
.dot-ok{color:#3ecfcf}.dot-off{color:#e05060}.dot-warn{color:#c8a84b}
.val-ok{color:#3ecfcf}.val-warn{color:#c8a84b}.val-err{color:#e05060}.val-dim{color:#3e4a60}
/* ── HTML/JS State Panel ── */
#htmlDiag{position:fixed;bottom:0;right:282px;z-index:999;width:240px;background:rgba(6,8,14,.97);border-left:1px solid var(--border);border-top:1px solid var(--border);overflow:hidden;font-family:'Share Tech Mono','Courier New',monospace;font-size:.58rem;box-shadow:-4px 0 20px rgba(0,0,0,.6);display:flex;flex-direction:column;max-height:100vh}
"""
html = html.replace('</style>', BRIDGE_CSS + '\n</style>', 1)

# ── 2. Panel div ───────────────────────────────────────────────────────────────
html = html.replace('</body>', '<div id="cppBridgeDiag"></div>\n<div id="htmlDiag"></div>\n</body>', 1)

# ── 3. Bridge JS ───────────────────────────────────────────────────────────────
BRIDGE_JS = r"""
// ═══════════════════════════════════════════════════════════════════════════
// C++ ENGINE BRIDGE  —  v43-bridge  (deep diagnostics)
// F6 = toggle panel
// ═══════════════════════════════════════════════════════════════════════════

const BRIDGE_URL = 'http://127.0.0.1:8765';

// ── Rolling average helper ──────────────────────────────────────────────────
function mkRing(n){ const a=[]; a._max=n; a._push=function(v){this.push(v);if(this.length>this._max)this.shift();}; a._avg=function(){return this.length?Math.round(this.reduce((s,x)=>s+x,0)/this.length):null;}; a._last=function(){return this.length?this[this.length-1]:null;}; return a; }

// ── Metrics state ───────────────────────────────────────────────────────────
const BM = {
  // bridge
  status:        'inactive',   // 'inactive' | 'active' | 'error'
  source:        'none',       // 'spawned' | 'none'
  lastEndpoint:  null,
  lastDuration:  null,
  bridgeTurns:   mkRing(20),   // engine AI turn latencies
  turnLoops:     mkRing(20),   // advanceTurn→advanceTurn intervals
  compactInterval: null,       // N/A in v43
  // render
  renderPulses:  mkRing(30),   // renderLoop call interval
  renderCosts:   mkRing(30),   // renderLoop execution time
  boardDrawMs:   null,
  ghostGlassMs:  null,         // N/A in v43 (no separate ghost pass)
  ghostPiecesMs: null,         // N/A
  cellGieldMs:   null,         // N/A
  cellPaintMs:   null,         // N/A
  sidebarMs:     null,
  sidebarLiteMs: null,         // N/A
  sidebarPlayersMs: null,
  sidebarScoreMs:   null,
  sidebarPawnsMs:   null,
  sidebarCelMs:     null,
  sidebarHintMs:    null,
  turnBarMs:     null,
  kingDangerMs:  null,
  slicesMs:      null,
  // compact (N/A in v43)
  compactExec:   null,
  compactResync: null,
  compactFin:    null,
  compactSeen:   0,
  quietHidden:   0,
  execMoveMs:    null,    // last execMove cost (ms)
  _resetTurnLoop: false,  // set by scheduleNext to clear stale _atLast after board build
  // player
  htmlPlayer:    null,
  bridgePlayer:  null,
  lastMove:      null,
  // errors
  desktopError:  null,
  lastError:     null,
  controlDrop:   0,
  // internal
  enabled:       false,
  connected:     false,
  pingMs:        -1,
  failCount:     0,
  totalTurns:    0,
  engineTurns:   0,
  jsTurns:       0,
  totalMoves:    0,
  moveTimes:     mkRing(20),
  gameId:        null,
  visible:       true,
  minimized:     false,
  log:           [],
  perPlayer:     {},
};

// ── Error capture ───────────────────────────────────────────────────────────
window.addEventListener('error', e => {
  BM.desktopError = e.message ? e.message.slice(0,60) : String(e);
  BM.lastError    = BM.desktopError;
  diagUpdate();
});
window.addEventListener('unhandledrejection', e => {
  BM.lastError = String(e.reason).slice(0,60);
  diagUpdate();
});

// ── Logging ─────────────────────────────────────────────────────────────────
function bmLog(msg, cls=''){
  const d=new Date();
  const ts=d.toLocaleTimeString('en',{hour12:false,hour:'2-digit',minute:'2-digit',second:'2-digit'});
  BM.log.push({ts,msg,cls});
  if(BM.log.length>30) BM.log.shift();
  diagUpdate();
}

// ── HTTP helper ──────────────────────────────────────────────────────────────
async function bridgeFetch(endpoint, body=null){
  BM.lastEndpoint = endpoint;
  const t0 = performance.now();
  const opts = body
    ? {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}
    : {method:'GET'};
  const r = await fetch(BRIDGE_URL+endpoint, {...opts, signal:AbortSignal.timeout(4000)});
  BM.lastDuration = Math.round(performance.now()-t0);
  if(!r.ok) throw new Error('HTTP '+r.status);
  return {data: await r.json(), dt: BM.lastDuration};
}

// ── Ping ────────────────────────────────────────────────────────────────────
async function bridgePing(){
  const t0 = performance.now();
  try {
    const r = await fetch(BRIDGE_URL+'/ping',{signal:AbortSignal.timeout(2000)});
    BM.pingMs    = Math.round(performance.now()-t0);
    BM.connected = r.ok;
    BM.source    = r.ok ? 'spawned' : 'none';
  } catch {
    BM.connected = false;
    BM.pingMs    = -1;
    BM.source    = 'none';
  }
  diagUpdate();
  return BM.connected;
}

// ── Init ─────────────────────────────────────────────────────────────────────
async function bridgeInit(){
  BM.enabled=false; BM.perPlayer={}; BM.bridgeTurns.length=0; BM.moveTimes.length=0;
  const alive = await bridgePing();
  if(!alive){ BM.status='inactive'; bmLog('Engine offline — JS AI','warn'); return; }
  const na=(typeof gNA==='function')?gNA():2;
  const np=(typeof gNP==='function')?gNP():2;
  BM.htmlPlayer  = 'P'+(((typeof G!=='undefined'&&G)?G.cur:0)+1);
  BM.bridgePlayer= BM.htmlPlayer;
  try {
    const {data,dt} = await bridgeFetch('/new_game',{dimensions:na,players:np});
    if(data.ok===false) throw new Error(data.error||'rejected');
    BM.gameId  = (data.state&&data.state.gameId)||'game1';
    BM.enabled = true;
    BM.status  = 'active';
    // Speed optimisations for AI spectator mode
    if(typeof WATCHMODE==='undefined'||WATCHMODE){
      // Collapse aiSpeed to minimum
      if(typeof aiSpeed!=='undefined') aiSpeed=150;
      // Kill trail/flash animations — they keep needsAnim=true which causes
      // requestAnimationFrame(renderLoop) to re-queue after every 513ms render,
      // blocking the event loop for 35+ seconds between turns.
      if(typeof showTrails!=='undefined')  showTrails=false;
      if(typeof turnTrails!=='undefined')  turnTrails=false;
      if(typeof G!=='undefined'&&G){       G.trail=[]; G.turnTrail=[]; G.flash=null; }
      if(typeof stopAnim==='function')     stopAnim();
      if(typeof needsAnim!=='undefined')   needsAnim=false;
    }
    bmLog('Engine ready ('+na+'D '+np+'p) '+dt+'ms','ok');
  } catch(e){
    BM.failCount++;
    BM.lastError=e.message;
    BM.status='error';
    bmLog('Init failed: '+e.message,'err');
  }
  diagUpdate();
}

// ── AI turn ──────────────────────────────────────────────────────────────────
async function bridgeAiTurn(pid){
  if(!BM.enabled||!BM.connected) return null;
  const t0=performance.now();
  BM.bridgePlayer='P'+(pid+1);
  try {
    // compact:1 skips the full serialize_state_json (avoids multi-MB response)
    const {data}=await bridgeFetch('/apply_ai_turn',{player:pid,game_id:BM.gameId,compact:'1'});
    const dt=Math.round(performance.now()-t0);
    BM.bridgeTurns._push(dt);
    BM.engineTurns++; BM.totalTurns++;
    if(!BM.perPlayer[pid]) BM.perPlayer[pid]={turns:0,totalMs:0,last:0};
    BM.perPlayer[pid].turns++;
    BM.perPlayer[pid].totalMs+=dt;
    BM.perPlayer[pid].last=dt;
    diagUpdate();
    // compact response: {ok,compact,turn:{...,move:{from:[],to:[]}}}
    // non-compact fallback: {ok,turn:{...,move:{from:[],to:[]}}}
    const mv = (data.turn && data.turn.move) || data.move || null;
    return mv;
  } catch(e){
    BM.failCount++; BM.lastError=e.message;
    BM.jsTurns++; BM.totalTurns++;
    bmLog('AI turn fallback: '+e.message,'warn');
    return null;
  }
}

// ── Sync move ────────────────────────────────────────────────────────────────
async function bridgeSyncMove(fromC,toC){
  if(!BM.enabled||!BM.connected) return;
  BM.lastMove = fromC.join(',')+'→'+toC.join(',');
  try {
    const {data,dt}=await bridgeFetch('/apply_move',{from:fromC,to:toC,game_id:BM.gameId});
    BM.moveTimes._push(dt); BM.totalMoves++;
  } catch(e){
    BM.failCount++; BM.lastError=e.message;
    bmLog('Sync failed: '+e.message,'warn');
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// RENDER INSTRUMENTATION — time every sub-function
// ═══════════════════════════════════════════════════════════════════════════
{
  // renderLoop — measures pulse (gap between calls) + cost (execution time)
  // Ignore samples taken during board build (>2000ms = init artifact)
  const _rl = renderLoop;
  let _rlLast = 0, _rlWarm = 0;
  renderLoop = function(){
    const now = performance.now();
    const pulse = _rlLast > 0 ? now - _rlLast : 0;
    _rlLast = now;
    _rl.apply(this, arguments);
    const cost = performance.now() - now;
    // Only record after warmup (skip first 3 calls) and filter outliers
    _rlWarm++;
    if(_rlWarm > 3 && pulse > 0 && pulse < 2000) BM.renderPulses._push(pulse);
    if(_rlWarm > 3 && cost < 1000) BM.renderCosts._push(cost);
  };
}
{
  const _db = drawBoard;
  drawBoard = function(){
    const t=performance.now(); _db.apply(this,arguments);
    const cost=performance.now()-t;
    if(cost < 1000) BM.boardDrawMs=cost;
  };
}
{
  const _rs = renderSidebar;
  renderSidebar = function(){
    const t=performance.now(); _rs.apply(this,arguments); BM.sidebarMs=performance.now()-t;
  };
}
{
  const _rt = renderTurnBar;
  renderTurnBar = function(){
    const t=performance.now(); _rt.apply(this,arguments); BM.turnBarMs=performance.now()-t;
  };
}
{
  const _rk = renderKingDanger;
  renderKingDanger = function(){
    const t=performance.now(); _rk.apply(this,arguments); BM.kingDangerMs=performance.now()-t;
  };
}
{
  const _sl = drawAllSlices;
  drawAllSlices = function(){
    const t=performance.now(); _sl.apply(this,arguments); BM.slicesMs=performance.now()-t;
  };
}

// ═══════════════════════════════════════════════════════════════════════════
// SPECTATOR RENDER THROTTLE
// Problem: execMove's renderAll() flushes 40+ accumulated rAF callbacks
// (each 430ms) = 17s of synchronous blocking per turn.
// Fix: make renderAll a dirty-flag setter during AI spectator mode;
//      do one controlled render per 500ms via setInterval (2fps is enough
//      to watch pieces move; the board state is always correct).
// ═══════════════════════════════════════════════════════════════════════════
{
  const _ra2 = renderAll;
  let _raOk = false;

  // boardBuilt(): true only after initGameAsync completes (HM.boardBuildEnd set by scheduleNext)
  function boardBuilt(){ return typeof HM!=='undefined' && HM.boardBuildEnd > 0; }

  // Also patch renderLoop — accumulated rAF callbacks from the 44s build phase
  // each block for 430ms when they fire after build ends. Making renderLoop a
  // no-op drains them instantly instead of blocking for 17s.
  {
    const _rl_prev = renderLoop;  // capture instrumented version
    renderLoop = function(){
      if(BM.enabled && typeof WATCHMODE!=='undefined' && WATCHMODE && !_raOk && boardBuilt()){
        if(typeof wglDirty!=='undefined') wglDirty=true;
        if(typeof needsAnim!=='undefined') needsAnim=false;
        return;  // drain queued rAFs instantly, no blocking
      }
      _rl_prev.apply(this, arguments);
    };
  }

  renderAll = function(){
    // Only throttle AFTER board build — during build, rAF drives chunk pacing
    // and stopAnim() would cancel it, slowing the build 10x.
    if(BM.enabled && typeof WATCHMODE!=='undefined' && WATCHMODE && !_raOk && boardBuilt()){
      if(typeof wglDirty!=='undefined') wglDirty = true;
      return;  // skip — periodic tick will render
    }
    _ra2.apply(this, arguments);
    // After any permitted render: stop rAF loop so it can't re-accumulate
    if(BM.enabled && typeof WATCHMODE!=='undefined' && WATCHMODE && boardBuilt()){
      if(typeof needsAnim!=='undefined') needsAnim = false;
      if(typeof stopAnim==='function') stopAnim();
    }
  };

  // 2fps render tick — the only path that actually draws in spectator mode (post-build)
  setInterval(()=>{
    if(!BM.enabled || typeof WATCHMODE==='undefined' || !WATCHMODE) return;
    if(!boardBuilt()) return;  // don't interfere with board build rAF pacing
    _raOk = true;
    _ra2.apply(renderAll, []);
    _raOk = false;
    if(typeof needsAnim!=='undefined') needsAnim = false;
    if(typeof stopAnim==='function') stopAnim();
  }, 500);
}

// ═══════════════════════════════════════════════════════════════════════════
// GAME FUNCTION PATCHES
// ═══════════════════════════════════════════════════════════════════════════
{
  // advanceTurn — measure turn loop interval
  const _at = advanceTurn;
  let _atLast = 0;
  advanceTurn = function(){
    const now=performance.now();
    if(BM._resetTurnLoop){ _atLast=now; BM._resetTurnLoop=false; }
    else if(_atLast>0) BM.turnLoops._push(now-_atLast);
    _atLast=now;
    // update player tracking
    if(typeof G!=='undefined'&&G){ BM.htmlPlayer='P'+(G.cur+1); }
    _at.apply(this,arguments);
  };
}
{
  // startGame — init bridge after game starts
  const _sg = startGame;
  startGame = function(){
    _sg.apply(this,arguments);
    setTimeout(bridgeInit, 350);
  };
}
// Speed patch: when bridge is active and WATCHMODE, collapse animation/AI delays.
// animDelay in execMove is 650ms for AI moves (hardcoded) — intercept via setTimeout.
// aiSpeed is a let variable — override it after bridgeInit completes.
{
  const _origSetTimeout = window.setTimeout;
  window.setTimeout = function(fn, delay, ...args){
    // Collapse the execMove animDelay (exactly 650ms) when bridge is active + watchmode
    if(BM.enabled && (typeof WATCHMODE!=='undefined') && WATCHMODE
       && delay===650 && typeof fn==='function'){
      delay = 0;
    }
    return _origSetTimeout.call(this, fn, delay, ...args);
  };
}
{
  // doAIMove — for 6D+ games: C++ engine only, NO JS AI fallback.
  // If engine is unavailable, pause and retry every 2s until it reconnects.
  // JS AI (_dm) is only called for <6D games where the engine is not used.
  const _dm = doAIMove;
  doAIMove = function(){
    const na=(typeof gNA==='function')?gNA():2;
    const pid=(typeof G!=='undefined'&&G&&G.cur!==undefined)?G.cur:-1;

    // C++ engine handles ALL dimensions — JS AI never used
    if(!BM.connected){
      // Engine offline — ping and retry in 2s
      bmLog('Engine offline — waiting to reconnect…','warn');
      bridgePing().then(()=>{ setTimeout(doAIMove, 2000); });
      return;
    }
    if(!BM.enabled){
      // Engine connected but not initialised — re-run init then retry
      bmLog('Engine not init — retrying bridgeInit…','warn');
      bridgeInit().then(()=>{ setTimeout(doAIMove, 1000); });
      return;
    }

    // Engine ready — request move
    bridgeAiTurn(pid).then(move=>{
      if(move&&Array.isArray(move.from)&&Array.isArray(move.to)){
        const fc=move.from, fk=gK(fc);
        if(typeof G!=='undefined'&&G&&G.pieces[fk]){
          const tk=gK(move.to);
          const target=G.pieces[tk]||null;
          const mv={
            coords:  move.to,
            cap:     !!target,
            capType: target?target.type:null,
            capPid:  target?target.pid:null,
          };
          const _t0=performance.now();
          try {
            execMove(fc,mv);
            BM.execMoveMs=Math.round(performance.now()-_t0);
            if(typeof G!=='undefined'&&G) G.flash=null;
            if(typeof stopAnim==='function') stopAnim();
            return;
          } catch(e1){
            BM.execMoveMs=Math.round(performance.now()-_t0);
            bmLog('execMove failed: '+e1.message+' — retrying','warn');
          }
        }
      }
      // Move was invalid/missing — retry this player's turn after brief delay
      BM.failCount++; diagUpdate();
      bmLog('Bad engine move — retrying in 1s','warn');
      setTimeout(doAIMove, 1000);
    }).catch(e=>{
      BM.failCount++; diagUpdate();
      bmLog('Engine error: '+(e&&e.message||e)+' — retry in 2s','warn');
      setTimeout(doAIMove, 2000);
    });
  };
}
{
  // execMove — sync human moves, track last move
  const _em = execMove;
  execMove = function(fc,mv){
    const pid=(typeof G!=='undefined'&&G&&G.cur!==undefined)?G.cur:-1;
    const wasHuman=(typeof isHuman==='function')&&isHuman(pid);
    BM.lastMove=[...fc].join(',')+'→'+[...mv.coords].join(',');
    _em.apply(this,arguments);
    if(wasHuman) bridgeSyncMove([...fc],[...mv.coords]);
  };
}

// ═══════════════════════════════════════════════════════════════════════════
// DIAGNOSTICS PANEL
// ═══════════════════════════════════════════════════════════════════════════

function _ms(v){ return v===null||v===undefined?'—':v<1?'<1ms':Math.round(v)+'ms'; }
function _avg(ring){ const a=ring._avg(); return a===null?'—':a+'ms'; }
function _bar(v, max, color){
  if(v===null||v===undefined) return '';
  const pct=Math.min(100,Math.round((v/max)*100));
  return '<div class="db-bar"><div class="db-bar-fill" style="width:'+pct+'%;background:'+color+'"></div></div>';
}
function _vc(v, warnMs, errMs){
  if(v===null||v===undefined) return 'val-dim';
  return v>=errMs?'val-err':v>=warnMs?'val-warn':'val-ok';
}

function diagRow(label, valHtml, barHtml){
  return '<div class="db-row"><span class="db-lbl">'+label+'</span>'
    +(barHtml||'<span style="flex:1"></span>')
    +'<span class="db-val">'+valHtml+'</span></div>';
}

function diagUpdate(){
  const el=document.getElementById('cppBridgeDiag');
  if(!el||!BM.visible){ if(el) el.innerHTML=''; return; }

  if(BM.minimized){
    const sd=BM.connected?'<span class="dot-ok">●</span>':'<span class="dot-off">●</span>';
    el.innerHTML='<div class="db-head"><span class="db-title">⚙ BRIDGE</span>'+sd
      +'<button class="db-btn" id="dbMin">□</button></div>';
    el.querySelector('#dbMin').addEventListener('click',()=>{BM.minimized=false;diagUpdate();});
    return;
  }

  const sd=BM.connected?'<span class="dot-ok">●</span>':'<span class="dot-off">●</span>';
  const na=(typeof gNA==='function')?gNA():'?';

  // ── compute all values once ──
  const avgBridge = BM.bridgeTurns._avg();
  const avgLoop   = BM.turnLoops._avg();
  const avgPulse  = BM.renderPulses._avg();
  const avgCost   = BM.renderCosts._avg();

  const statusTxt = BM.status==='active'
    ? '<span class="val-ok">active</span>'
    : BM.status==='error'
      ? '<span class="val-err">error</span>'
      : '<span class="val-dim">inactive</span>';

  const sourceTxt = BM.enabled
    ? '<span class="val-ok">spawned</span>'
    : '<span class="val-dim">'+BM.source+'</span>';

  const modeTxt = BM.enabled
    ? '<span class="val-ok">C++ ENGINE</span>'
    : '<span class="val-err">JS FALLBACK</span>';

  // log (last 8 lines)
  const logHtml = BM.log.slice().reverse().slice(0,8).map(e=>{
    const c=e.cls==='ok'?'#3ecfcf':e.cls==='err'?'#e05060':e.cls==='warn'?'#c8a84b':'#3e4a60';
    return '<div style="color:'+c+';font-size:.52rem;padding:.06rem .45rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">['+e.ts+'] '+e.msg+'</div>';
  }).join('')||'<div style="color:#3e4a60;font-size:.52rem;padding:.06rem .45rem">—</div>';

  // per-player table
  let ppHtml='';
  const pids=Object.keys(BM.perPlayer).map(Number).sort((a,b)=>a-b);
  if(pids.length){
    ppHtml='<div class="db-sec">PER-PLAYER</div>'
      +'<div style="display:grid;grid-template-columns:1fr 1fr 1fr 1fr;font-size:.5rem;padding:.06rem .45rem;color:#3e4a60">'+
      '<span>Player</span><span>Turns</span><span>Last</span><span>Avg</span></div>';
    for(const p of pids){
      const s=BM.perPlayer[p];
      const avg=s.turns?Math.round(s.totalMs/s.turns):0;
      const col=(typeof PC!=='undefined')?PC[p]:'var(--ink)';
      ppHtml+='<div style="display:grid;grid-template-columns:1fr 1fr 1fr 1fr;font-size:.52rem;padding:.04rem .45rem">'
        +'<span style="color:'+col+'">P'+(p+1)+'</span>'
        +'<span style="color:var(--ink)">'+s.turns+'</span>'
        +'<span style="color:#3ecfcf">'+s.last+'ms</span>'
        +'<span style="color:#3e4a60">'+avg+'ms</span></div>';
    }
  }

  el.innerHTML =
`<div class="db-head">
  <span class="db-title">⚙ C++ BRIDGE DIAGNOSTICS</span>
  ${sd}
  <button class="db-btn" id="dbMin" title="−">−</button>
  <button class="db-btn" id="dbClose" title="F6">✕</button>
</div>
<div class="db-scroll">

  <div class="db-sec">BRIDGE</div>
  ${diagRow('Status', statusTxt+' / source: '+sourceTxt)}
  ${diagRow('Mode', modeTxt)}
  ${diagRow('Dimensions', '<span style="color:var(--ink)">'+(na)+'D / '+BM.engineTurns+'/'+BM.totalTurns+' eng</span>')}
  ${diagRow('Last endpoint', BM.lastEndpoint?'<span style="color:#3e4a60">'+BM.lastEndpoint+'</span>':'<span class="val-dim">—</span>')}
  ${diagRow('Duration', _ms(BM.lastDuration), _bar(BM.lastDuration,500,'#3ecfcf'))}
  ${diagRow('execMove cost', BM.execMoveMs!=null?'<span class="'+_vc(BM.execMoveMs,500,5000)+'">'+BM.execMoveMs+'ms</span>':'<span class="val-dim">—</span>', _bar(BM.execMoveMs,2000,'#c8a84b'))}
  ${diagRow('Avg bridge turn', avgBridge===null?'<span class="val-dim">—</span>':'<span class="'+_vc(avgBridge,200,800)+'">'+avgBridge+'ms</span>', _bar(avgBridge,1000,'#3ecfcf'))}
  ${diagRow('Avg turn loop', avgLoop===null?'<span class="val-dim">—</span>':'<span class="'+_vc(avgLoop,500,2000)+'">'+avgLoop+'ms</span>', _bar(avgLoop,3000,'#c8a84b'))}
  ${diagRow('Avg compact move', '<span class="val-dim">—</span>')}
  ${diagRow('Ping', BM.pingMs>=0?'<span class="'+_vc(BM.pingMs,50,200)+'">'+BM.pingMs+'ms</span>':'<span class="val-err">—</span>')}
  ${diagRow('Failures', BM.failCount>0?'<span class="val-err">'+BM.failCount+'</span>':'<span class="val-dim">0</span>')}

  <div class="db-sec">RENDER</div>
  ${diagRow('Avg render pulse', avgPulse===null?'<span class="val-dim">—</span>':'<span class="'+_vc(avgPulse,33,100)+'">'+avgPulse+'ms</span>', _bar(avgPulse,100,'#3ecfcf'))}
  ${diagRow('Avg render cost', avgCost===null?'<span class="val-dim">—</span>':'<span class="'+_vc(avgCost,16,50)+'">'+avgCost+'ms</span>', _bar(avgCost,60,'#e05060'))}
  ${diagRow('Board draw', _ms(BM.boardDrawMs), _bar(BM.boardDrawMs,50,'#3ecfcf'))}
  ${diagRow('Ghost glass', '<span class="val-dim">—</span>')}
  ${diagRow('Ghost pieces', '<span class="val-dim">—</span>')}
  ${diagRow('Cell gield', '<span class="val-dim">—</span>')}
  ${diagRow('Cell paint', '<span class="val-dim">—</span>')}

  <div class="db-sec">SIDEBAR / UI</div>
  ${diagRow('Sidebar', _ms(BM.sidebarMs), _bar(BM.sidebarMs,20,'#3ecfcf'))}
  ${diagRow('Sidebar lite', '<span class="val-dim">—</span>')}
  ${diagRow('Sidebar players', '<span class="val-dim">—</span>')}
  ${diagRow('Sidebar score', '<span class="val-dim">—</span>')}
  ${diagRow('Sidebar pawns', '<span class="val-dim">—</span>')}
  ${diagRow('Sidebar cel', '<span class="val-dim">—</span>')}
  ${diagRow('Sidebar hint', '<span class="val-dim">—</span>')}
  ${diagRow('Turn bar', _ms(BM.turnBarMs), _bar(BM.turnBarMs,5,'#3ecfcf'))}
  ${diagRow('King danger', _ms(BM.kingDangerMs))}
  ${diagRow('Slices', BM.slicesMs!==null?_ms(BM.slicesMs):'<span class="val-dim">—</span>')}

  <div class="db-sec">COMPACT</div>
  ${diagRow('Compact exec', '<span class="val-dim">—</span>')}
  ${diagRow('Compact resync', '<span class="val-dim">—</span>')}
  ${diagRow('Compact finalize', '<span class="val-dim">—</span>')}
  ${diagRow('Compact moves seen', '<span style="color:var(--ink)">'+BM.compactSeen+'</span>')}
  ${diagRow('Quiet moves hidden', '<span style="color:var(--ink)">'+BM.quietHidden+'</span>')}

  <div class="db-sec">PLAYERS / MOVES</div>
  ${diagRow('HTML player', BM.htmlPlayer?'<span style="color:var(--ink)">'+BM.htmlPlayer+'</span>':'<span class="val-dim">—</span>')}
  ${diagRow('Bridge player', BM.bridgePlayer?'<span style="color:#3ecfcf">'+BM.bridgePlayer+'</span>':'<span class="val-dim">—</span>')}
  ${diagRow('Last move', BM.lastMove?'<span style="color:#3e4a60;font-size:.5rem">'+BM.lastMove+'</span>':'<span class="val-dim">—</span>')}
  ${diagRow('Move syncs', '<span style="color:var(--muted)">'+BM.totalMoves+'</span>')}

  <div class="db-sec">ERRORS</div>
  ${diagRow('Desktop error', BM.desktopError?'<span class="val-err" style="font-size:.5rem;word-break:break-all">'+BM.desktopError.slice(0,36)+'</span>':'<span class="val-dim">—</span>')}
  ${diagRow('Last error', BM.lastError?'<span class="val-err" style="font-size:.5rem">'+BM.lastError.slice(0,36)+'</span>':'<span class="val-dim">—</span>')}
  ${diagRow('Control drop', '<span class="val-dim">'+BM.controlDrop+'</span>')}

  ${ppHtml}

  <div class="db-sec">LOG</div>
  ${logHtml}

</div>
<div class="db-footer">
  <button class="db-btn" id="dbRestart" style="flex:1">↺ RESTART ENGINE</button>
  <button class="db-btn" id="dbPing">⊙ PING</button>
  <div style="width:100%;font-size:.45rem;color:#3e4a60;text-align:center;padding-top:.1rem">F6 toggle · :8765</div>
</div>`;

  el.querySelector('#dbMin').addEventListener('click',()=>{BM.minimized=true;diagUpdate();});
  el.querySelector('#dbClose').addEventListener('click',()=>{BM.visible=false;diagUpdate();});
  el.querySelector('#dbPing').addEventListener('click', bridgePing);
  el.querySelector('#dbRestart').addEventListener('click', async()=>{
    bmLog('Restarting engine…','warn'); diagUpdate();
    if(window.electronAPI&&window.electronAPI.restartBridge){
      try{ const r=await window.electronAPI.restartBridge();
        bmLog(r.ok?'Restart OK':'Restart failed: '+(r.lastError||'?'),r.ok?'ok':'err');
      } catch(e){ bmLog('Restart error: '+e.message,'err'); }
    } else { await bridgeInit(); }
    diagUpdate();
  });
}

// ═══════════════════════════════════════════════════════════════════════════
// HTML / JS GAME STATE PANEL  —  F7 to toggle
// ═══════════════════════════════════════════════════════════════════════════

const HM = {
  visible:       true,
  minimized:     false,
  doAIMoves:     0,
  advanceTurns:  0,
  execMoves:     0,
  schedules:     0,
  lastAiStart:   0,
  lastMoveTs:    0,
  boardBuildStart: performance.now(),
  boardBuildEnd:   0,     // set when scheduleNext first fires
  firstSchedule:   true,  // flag to ignore board-build interval in avgTurnLoop
};

// Wrap scheduleNext to count calls and mark board-build end
{
  const _sn = scheduleNext;
  scheduleNext = function(){
    HM.schedules++;
    if(HM.firstSchedule){
      HM.boardBuildEnd = performance.now();
      HM.firstSchedule = false;
      // Reset turn loop ring AND stale _atLast so board-build gap doesn't pollute avg
      BM.turnLoops.length = 0;
      BM._resetTurnLoop = true;
    }
    _sn.apply(this, arguments);
  };
}

// Count doAIMove calls and track JS AI timing
{
  const _origDoAI = doAIMove;
  // Note: doAIMove was already patched by bridge code above; wrap that patch
  const _bridgeDoAI = doAIMove;
  doAIMove = function(){
    HM.doAIMoves++;
    HM.lastAiStart = performance.now();
    _bridgeDoAI.apply(this, arguments);
  };
}

// Count execMove calls and track stall time
{
  const _origEx = execMove;
  const _bridgeEx = execMove;
  execMove = function(fc, mv){
    HM.execMoves++;
    HM.lastMoveTs = performance.now();
    _bridgeEx.apply(this, arguments);
  };
}

// Count advanceTurn
{
  const _origAt = advanceTurn;
  const _bridgeAt = advanceTurn;
  advanceTurn = function(){
    HM.advanceTurns++;
    _bridgeAt.apply(this, arguments);
  };
}

function _ago(ms){
  if(!ms) return '—';
  const s=Math.round((performance.now()-ms)/1000);
  return s<60?s+'s ago':Math.floor(s/60)+'m'+Math.round(s%60)+'s ago';
}

function htmlDiagUpdate(){
  const el=document.getElementById('htmlDiag');
  if(!el||!HM.visible){ if(el) el.innerHTML=''; return; }

  if(HM.minimized){
    el.innerHTML='<div class="db-head"><span class="db-title">⬡ HTML STATE</span>'
      +'<button class="db-btn" id="hdMin">□</button></div>';
    el.querySelector('#hdMin').addEventListener('click',()=>{HM.minimized=false;htmlDiagUpdate();});
    return;
  }

  // Live game state reads
  const gExists = typeof G!=='undefined' && !!G;
  const cur     = gExists ? G.cur : null;
  const gameOver= gExists ? G.gameOver : null;
  const pieces  = gExists ? Object.keys(G.pieces||{}).length : null;
  const legal   = gExists ? (G.legal||[]).length : null;
  const flash   = gExists ? !!G.flash : null;
  const ml      = typeof moveLock!=='undefined' ? moveLock : null;
  const pa      = typeof paused!=='undefined' ? paused : null;
  const wm      = typeof WATCHMODE!=='undefined' ? WATCHMODE : null;
  const na      = typeof gNA==='function' ? gNA() : null;
  const np      = typeof gNP==='function' ? gNP() : null;
  const ai_spd  = typeof aiSpeed!=='undefined' ? aiSpeed : null;
  const timer   = typeof aiTimer!=='undefined' ? !!aiTimer : null;
  const isHum   = (gExists && cur!==null && typeof isHuman==='function') ? isHuman(cur) : null;
  const alive   = typeof activePids!=='undefined' ? activePids.filter(p=>typeof hasKing==='function'&&hasKing(p)).length : null;
  const stalled = HM.lastMoveTs ? Math.round(performance.now()-HM.lastMoveTs) : null;

  function bval(v){ return v===null?'<span class="val-dim">—</span>':v?'<span class="val-ok">true</span>':'<span class="val-warn">false</span>'; }
  function nval(v,warn,err){ if(v===null)return '<span class="val-dim">—</span>';const c=err&&v>=err?'val-err':warn&&v>=warn?'val-warn':'val-ok';return '<span class="'+c+'">'+v+'</span>'; }
  function sval(v){ return v===null?'<span class="val-dim">—</span>':'<span style="color:var(--ink)">'+v+'</span>'; }

  el.innerHTML =
`<div class="db-head">
  <span class="db-title">⬡ HTML / JS GAME STATE</span>
  <button class="db-btn" id="hdMin">−</button>
  <button class="db-btn" id="hdClose">✕</button>
</div>
<div class="db-scroll">

  <div class="db-sec">GAME STATE</div>
  ${diagRow('G exists', bval(gExists))}
  ${diagRow('G.cur (player)', sval(cur!==null?'P'+(cur+1):null))}
  ${diagRow('isHuman(cur)', bval(isHum))}
  ${diagRow('gameOver', bval(gameOver))}
  ${diagRow('WATCHMODE', bval(wm))}
  ${diagRow('moveLock', ml?'<span class="val-err">LOCKED</span>':'<span class="val-ok">free</span>')  }
  ${diagRow('paused', pa?'<span class="val-warn">paused</span>':'<span class="val-ok">running</span>')}
  ${diagRow('aiTimer set', bval(timer))}
  ${diagRow('Dimensions', sval(na?na+'D':null))}
  ${diagRow('Players (np)', sval(np))}
  ${diagRow('Alive', sval(alive))}
  ${diagRow('Pieces on board', nval(pieces,5000,50000))}
  ${diagRow('Legal moves', sval(legal))}
  ${diagRow('aiSpeed', sval(ai_spd?ai_spd+'ms':null))}

  <div class="db-sec">CALL COUNTS</div>
  ${diagRow('scheduleNext', nval(HM.schedules))}
  ${diagRow('doAIMove', nval(HM.doAIMoves))}
  ${diagRow('advanceTurn', nval(HM.advanceTurns))}
  ${diagRow('execMove', nval(HM.execMoves))}

  <div class="db-sec">TIMING</div>
  ${diagRow('Board build', HM.boardBuildEnd?'<span class="val-ok">'+Math.round((HM.boardBuildEnd-HM.boardBuildStart)/1000)+'s</span>':HM.firstSchedule?'<span class="val-warn">building…</span>':'<span class="val-dim">—</span>')}
  ${diagRow('Last move', HM.lastMoveTs?'<span style="color:var(--muted)">'+_ago(HM.lastMoveTs)+'</span>':'<span class="val-dim">—</span>')}
  ${diagRow('Stalled for', stalled!==null?'<span class="'+(stalled>60000?'val-err':stalled>15000?'val-warn':'val-ok')+'">'+Math.round(stalled/1000)+'s</span>':'<span class="val-dim">—</span>')}
  ${diagRow('Engine computing', (HM.lastAiStart&&BM.enabled&&(!HM.lastMoveTs||HM.lastAiStart>HM.lastMoveTs)&&(performance.now()-HM.lastAiStart)>500)?'<span class="val-warn">'+Math.round((performance.now()-HM.lastAiStart)/1000)+'s…</span>':'<span class="val-dim">—</span>')}
  ${diagRow('Last AI started', HM.lastAiStart?'<span style="color:var(--muted)">'+_ago(HM.lastAiStart)+'</span>':'<span class="val-dim">—</span>')}

  <div class="db-sec">BRIDGE / JS SPLIT</div>
  ${diagRow('Engine turns', nval(BM.engineTurns))}
  ${diagRow('JS fallback turns', BM.jsTurns>0?'<span class="val-warn">'+BM.jsTurns+'</span>':'<span class="val-ok">0</span>')}
  ${diagRow('BM.enabled', bval(BM.enabled))}
  ${diagRow('BM.connected', bval(BM.connected))}
  ${diagRow('USE_ENGINE?', (BM.enabled&&BM.connected)?'<span class="val-ok">YES</span>':'<span class="val-err">NO — waiting</span>')}

</div>
<div class="db-footer">
  <div style="font-size:.45rem;color:#3e4a60;text-align:center;width:100%">F7 toggle · auto-refresh 1s</div>
</div>`;

  el.querySelector('#hdMin').addEventListener('click',()=>{HM.minimized=true;htmlDiagUpdate();});
  el.querySelector('#hdClose').addEventListener('click',()=>{HM.visible=false;htmlDiagUpdate();});
}

// Auto-refresh HTML panel every second
setInterval(htmlDiagUpdate, 1000);

// ── Keyboard shortcut ────────────────────────────────────────────────────────
document.addEventListener('keydown', e=>{
  if(e.key==='F6'){ e.preventDefault(); BM.visible=!BM.visible; BM.minimized=false; diagUpdate(); }
  if(e.key==='F7'){ e.preventDefault(); HM.visible=!HM.visible; HM.minimized=false; htmlDiagUpdate(); }
});

// ── Periodic ping ────────────────────────────────────────────────────────────
setInterval(()=>{ if(!BM.connected) bridgePing(); }, 3000);
setInterval(()=>{ if(BM.connected&&BM.visible) bridgePing(); }, 8000);

// ── Boot ─────────────────────────────────────────────────────────────────────
window.addEventListener('DOMContentLoaded', ()=>{
  setTimeout(()=>{ bridgePing(); diagUpdate(); }, 800);
});
"""

last_sc = html.rfind('</script>')
if last_sc == -1:
    print("ERROR: no </script>", file=sys.stderr); sys.exit(1)

html = html[:last_sc] + BRIDGE_JS + '\n' + html[last_sc:]

# ── 4. Title ──────────────────────────────────────────────────────────────────
html = html.replace('<title>8D Chess v16</title>', '<title>8D Chess v43-bridge</title>', 1)

with open(DEST, 'w', encoding='utf-8') as f:
    f.write(html)
print("Written:", DEST)
