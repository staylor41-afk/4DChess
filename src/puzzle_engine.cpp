// puzzle_engine.cpp — N-dimensional collapse puzzle HTTP server
// Compile (Linux/Mac): g++ -std=c++17 -O2 -o puzzle_engine puzzle_engine.cpp
// Compile (Windows):   cl /std:c++17 /O2 puzzle_engine.cpp /link ws2_32.lib
// Usage: ./puzzle_engine [port]   (default port 8770)

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef int socklen_t;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
   typedef int SOCKET;
#  define INVALID_SOCKET (-1)
#  define SOCKET_ERROR   (-1)
#  define closesocket    close
#endif

#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Data model
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int MAX_NDIM       = 8;
static constexpr int MAX_DIAG_PAIRS = 12;
static constexpr int MAX_K_PAIRS    = 8;

struct Piece {
    int  id;
    char type; // R N B Q K P
    std::vector<int> pos;
};

struct Board {
    int ndim  = 3;
    int bsize = 8;
    std::vector<Piece> pieces;
};

struct Move {
    int capturerIdx;
    int capturedIdx;
    std::vector<int> from;
    std::vector<int> to;
};

// ─────────────────────────────────────────────────────────────────────────────
// JSON helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string posToJson(const std::vector<int>& pos) {
    std::ostringstream ss;
    ss << "[";
    for (int i = 0; i < (int)pos.size(); ++i) {
        if (i) ss << ",";
        ss << pos[i];
    }
    ss << "]";
    return ss.str();
}

static std::string pieceToJson(const Piece& p) {
    std::ostringstream ss;
    ss << "{\"id\":" << p.id
       << ",\"type\":\"" << p.type << "\""
       << ",\"pos\":" << posToJson(p.pos)
       << "}";
    return ss.str();
}

static std::string boardToJson(const Board& b) {
    std::ostringstream ss;
    ss << "{\"ndim\":" << b.ndim << ",\"bsize\":" << b.bsize << ",\"pieces\":[";
    for (int i = 0; i < (int)b.pieces.size(); ++i) {
        if (i) ss << ",";
        ss << pieceToJson(b.pieces[i]);
    }
    ss << "]}";
    return ss.str();
}

static std::string moveToJson(const Move& m) {
    std::ostringstream ss;
    ss << "{\"capturer_idx\":" << m.capturerIdx
       << ",\"captured_idx\":" << m.capturedIdx
       << ",\"from\":" << posToJson(m.from)
       << ",\"to\":" << posToJson(m.to)
       << "}";
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Simple JSON parser helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string parseStrField(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto p = json.find(needle);
    if (p == std::string::npos) return "";
    p += needle.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':')) ++p;
    if (p >= json.size() || json[p] == 'n') return ""; // null
    if (json[p] != '"') return "";
    ++p;
    std::string val;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\') { ++p; if (p < json.size()) val += json[p]; }
        else val += json[p];
        ++p;
    }
    return val;
}

static int parseIntField(const std::string& json, const std::string& key, int def = -999) {
    std::string needle = "\"" + key + "\"";
    auto p = json.find(needle);
    if (p == std::string::npos) return def;
    p += needle.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':')) ++p;
    if (p >= json.size()) return def;
    if (json[p] == 'n') return def; // null
    bool neg = false;
    if (json[p] == '-') { neg = true; ++p; }
    int val = 0; bool found = false;
    while (p < json.size() && json[p] >= '0' && json[p] <= '9') {
        val = val * 10 + (json[p] - '0'); ++p; found = true;
    }
    return found ? (neg ? -val : val) : def;
}

static std::vector<int> parseIntArray(const std::string& json, size_t& pos) {
    std::vector<int> result;
    while (pos < json.size() && json[pos] != '[') ++pos;
    if (pos >= json.size()) return result;
    ++pos; // skip '['
    while (pos < json.size() && json[pos] != ']') {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ',')) ++pos;
        if (pos < json.size() && json[pos] == ']') break;
        bool neg = false;
        if (pos < json.size() && json[pos] == '-') { neg = true; ++pos; }
        int v = 0; bool found = false;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
            v = v * 10 + (json[pos] - '0'); ++pos; found = true;
        }
        if (found) result.push_back(neg ? -v : v);
    }
    if (pos < json.size()) ++pos; // skip ']'
    return result;
}

static Board parseBoard(const std::string& json) {
    Board b;
    b.ndim  = parseIntField(json, "ndim",  3);
    b.bsize = parseIntField(json, "bsize", 8);

    // find "pieces":[...]
    auto pp = json.find("\"pieces\"");
    if (pp == std::string::npos) return b;
    while (pp < json.size() && json[pp] != '[') ++pp;
    if (pp >= json.size()) return b;
    ++pp; // skip outer '['

    while (pp < json.size()) {
        while (pp < json.size() && json[pp] != '{' && json[pp] != ']') ++pp;
        if (pp >= json.size() || json[pp] == ']') break;
        ++pp; // skip '{'

        // parse one piece object
        Piece pc;
        pc.id   = -1;
        pc.type = 'R';
        // find fields within braces
        size_t end = pp;
        int depth = 1;
        while (end < json.size() && depth > 0) {
            if (json[end] == '{') ++depth;
            else if (json[end] == '}') --depth;
            ++end;
        }
        std::string obj = json.substr(pp, end - pp - 1);
        pc.id   = parseIntField(obj, "id", 0);
        std::string t = parseStrField(obj, "type");
        if (!t.empty()) pc.type = t[0];
        // parse pos array
        auto posp = obj.find("\"pos\"");
        if (posp != std::string::npos) {
            size_t ap = posp;
            pc.pos = parseIntArray(obj, ap);
        }
        b.pieces.push_back(pc);
        pp = end;
    }
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// Move generation
// ─────────────────────────────────────────────────────────────────────────────

static bool posEq(const std::vector<int>& a, const std::vector<int>& b) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < (int)a.size(); ++i) if (a[i] != b[i]) return false;
    return true;
}

static bool inBounds(const std::vector<int>& pos, int bsize) {
    for (int v : pos) if (v < 0 || v >= bsize) return false;
    return true;
}

// returns index of piece at pos, or -1
static int pieceAt(const Board& board, const std::vector<int>& pos) {
    for (int i = 0; i < (int)board.pieces.size(); ++i)
        if (posEq(board.pieces[i].pos, pos)) return i;
    return -1;
}

static std::vector<Move> rookMoves(const Board& board, int idx) {
    std::vector<Move> moves;
    const Piece& p = board.pieces[idx];
    int ndim = board.ndim;
    for (int axis = 0; axis < ndim; ++axis) {
        for (int dir : {-1, 1}) {
            std::vector<int> cur = p.pos;
            while (true) {
                cur[axis] += dir;
                if (!inBounds(cur, board.bsize)) break;
                int hit = pieceAt(board, cur);
                if (hit >= 0) {
                    moves.push_back({idx, hit, p.pos, cur});
                    break;
                }
            }
        }
    }
    return moves;
}

static std::vector<Move> bishopMoves(const Board& board, int idx) {
    std::vector<Move> moves;
    const Piece& p = board.pieces[idx];
    int ndim = board.ndim;
    int pairs = 0;
    for (int a = 0; a < ndim && pairs < MAX_DIAG_PAIRS; ++a) {
        for (int b2 = a + 1; b2 < ndim && pairs < MAX_DIAG_PAIRS; ++b2, ++pairs) {
            for (int da : {-1, 1}) {
                for (int db : {-1, 1}) {
                    std::vector<int> cur = p.pos;
                    while (true) {
                        cur[a]  += da;
                        cur[b2] += db;
                        if (!inBounds(cur, board.bsize)) break;
                        int hit = pieceAt(board, cur);
                        if (hit >= 0) {
                            moves.push_back({idx, hit, p.pos, cur});
                            break;
                        }
                    }
                }
            }
        }
    }
    return moves;
}

static std::vector<Move> knightMoves(const Board& board, int idx) {
    std::vector<Move> moves;
    const Piece& p = board.pieces[idx];
    int ndim = board.ndim;
    for (int a = 0; a < ndim; ++a) {
        for (int b2 = 0; b2 < ndim; ++b2) {
            if (a == b2) continue;
            for (int da : {-2, 2}) {
                for (int db : {-1, 1}) {
                    std::vector<int> cur = p.pos;
                    cur[a]  += da;
                    cur[b2] += db;
                    if (!inBounds(cur, board.bsize)) continue;
                    int hit = pieceAt(board, cur);
                    if (hit >= 0) moves.push_back({idx, hit, p.pos, cur});
                }
            }
        }
    }
    return moves;
}

static std::vector<Move> kingMoves(const Board& board, int idx) {
    std::vector<Move> moves;
    const Piece& p = board.pieces[idx];
    int ndim = board.ndim;
    // axial (1 step each axis)
    for (int axis = 0; axis < ndim; ++axis) {
        for (int dir : {-1, 1}) {
            std::vector<int> cur = p.pos;
            cur[axis] += dir;
            if (!inBounds(cur, board.bsize)) continue;
            int hit = pieceAt(board, cur);
            if (hit >= 0) moves.push_back({idx, hit, p.pos, cur});
        }
    }
    // diagonal pairs (1 step)
    int pairs = 0;
    for (int a = 0; a < ndim && pairs < MAX_K_PAIRS; ++a) {
        for (int b2 = a + 1; b2 < ndim && pairs < MAX_K_PAIRS; ++b2, ++pairs) {
            for (int da : {-1, 1}) {
                for (int db : {-1, 1}) {
                    std::vector<int> cur = p.pos;
                    cur[a]  += da;
                    cur[b2] += db;
                    if (!inBounds(cur, board.bsize)) continue;
                    int hit = pieceAt(board, cur);
                    if (hit >= 0) moves.push_back({idx, hit, p.pos, cur});
                }
            }
        }
    }
    return moves;
}

static std::vector<Move> pawnMoves(const Board& board, int idx) {
    // simplified neutral pawn: 1 step any direction (capture only)
    std::vector<Move> moves;
    const Piece& p = board.pieces[idx];
    int ndim = board.ndim;
    for (int axis = 0; axis < ndim; ++axis) {
        for (int dir : {-1, 1}) {
            std::vector<int> cur = p.pos;
            cur[axis] += dir;
            if (!inBounds(cur, board.bsize)) continue;
            int hit = pieceAt(board, cur);
            if (hit >= 0) moves.push_back({idx, hit, p.pos, cur});
        }
    }
    return moves;
}

static std::vector<Move> legalCaptures(const Board& board, int idx) {
    char t = board.pieces[idx].type;
    switch (t) {
        case 'R': return rookMoves(board, idx);
        case 'B': return bishopMoves(board, idx);
        case 'Q': {
            auto r = rookMoves(board, idx);
            auto b = bishopMoves(board, idx);
            r.insert(r.end(), b.begin(), b.end());
            return r;
        }
        case 'N': return knightMoves(board, idx);
        case 'K': return kingMoves(board, idx);
        case 'P': return pawnMoves(board, idx);
        default:  return {};
    }
}

static std::vector<Move> allLegalCaptures(const Board& board) {
    std::vector<Move> all;
    for (int i = 0; i < (int)board.pieces.size(); ++i) {
        auto mv = legalCaptures(board, i);
        all.insert(all.end(), mv.begin(), mv.end());
    }
    return all;
}

// ─────────────────────────────────────────────────────────────────────────────
// Board hashing / apply move
// ─────────────────────────────────────────────────────────────────────────────

static size_t hashBoard(const Board& board) {
    // hash by sorted (type, pos) pairs
    std::vector<std::string> tokens;
    for (const Piece& p : board.pieces) {
        std::string tok(1, p.type);
        for (int v : p.pos) tok += "," + std::to_string(v);
        tokens.push_back(tok);
    }
    std::sort(tokens.begin(), tokens.end());
    std::string concat;
    for (const auto& s : tokens) concat += s + "|";
    return std::hash<std::string>{}(concat);
}

static Board applyMove(const Board& board, const Move& m) {
    Board nb = board;
    // remove captured piece
    nb.pieces.erase(nb.pieces.begin() + m.capturedIdx);
    // find capturer (index may have shifted)
    int ci = m.capturerIdx;
    if (m.capturedIdx < m.capturerIdx) --ci;
    nb.pieces[ci].pos = m.to;
    return nb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Solver
// ─────────────────────────────────────────────────────────────────────────────

static bool findSolution(const Board& board,
                         std::vector<Move>& solution,
                         std::unordered_set<size_t>& deadEnds) {
    if (board.pieces.size() == 1) return true;
    size_t h = hashBoard(board);
    if (deadEnds.count(h)) return false;

    auto moves = allLegalCaptures(board);
    if (moves.empty()) { deadEnds.insert(h); return false; }

    for (const Move& m : moves) {
        Board nb = applyMove(board, m);
        solution.push_back(m);
        if (findSolution(nb, solution, deadEnds)) return true;
        solution.pop_back();
    }
    deadEnds.insert(h);
    return false;
}

static int countSolutions(const Board& board, int limit,
                           std::unordered_set<size_t>& cache) {
    if (board.pieces.size() == 1) return 1;
    size_t h = hashBoard(board);
    if (cache.count(h)) return 0;

    auto moves = allLegalCaptures(board);
    if (moves.empty()) { cache.insert(h); return 0; }

    int total = 0;
    for (const Move& m : moves) {
        Board nb = applyMove(board, m);
        total += countSolutions(nb, limit, cache);
        if (total >= limit) return total;
    }
    if (total == 0) cache.insert(h);
    return total;
}

// ─────────────────────────────────────────────────────────────────────────────
// Reachable squares (for reverse-build)
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<std::vector<int>> reachableSquares(char type, const std::vector<int>& from,
                                                        int ndim, int bsize) {
    // Build a temporary board with just this one piece to find where it can move
    // (ignoring other pieces — empty board reach)
    Board tmp;
    tmp.ndim  = ndim;
    tmp.bsize = bsize;
    tmp.pieces.push_back({0, type, from});

    std::vector<std::vector<int>> squares;
    // We enumerate all board positions and check if a straight path exists
    // For sliding pieces we do a walk; for knight/king we do offsets

    auto addSlide = [&](int axis, int dir) {
        std::vector<int> cur = from;
        while (true) {
            cur[axis] += dir;
            if (!inBounds(cur, bsize)) break;
            squares.push_back(cur);
        }
    };
    auto addDiag = [&](int a, int b2, int da, int db) {
        std::vector<int> cur = from;
        while (true) {
            cur[a]  += da;
            cur[b2] += db;
            if (!inBounds(cur, bsize)) break;
            squares.push_back(cur);
        }
    };

    switch (type) {
        case 'R':
            for (int ax = 0; ax < ndim; ++ax)
                for (int d : {-1, 1}) addSlide(ax, d);
            break;
        case 'B': {
            int pairs = 0;
            for (int a = 0; a < ndim && pairs < MAX_DIAG_PAIRS; ++a)
                for (int b2 = a+1; b2 < ndim && pairs < MAX_DIAG_PAIRS; ++b2, ++pairs)
                    for (int da : {-1,1}) for (int db : {-1,1}) addDiag(a,b2,da,db);
            break;
        }
        case 'Q': {
            for (int ax = 0; ax < ndim; ++ax)
                for (int d : {-1, 1}) addSlide(ax, d);
            int pairs = 0;
            for (int a = 0; a < ndim && pairs < MAX_DIAG_PAIRS; ++a)
                for (int b2 = a+1; b2 < ndim && pairs < MAX_DIAG_PAIRS; ++b2, ++pairs)
                    for (int da : {-1,1}) for (int db : {-1,1}) addDiag(a,b2,da,db);
            break;
        }
        case 'N':
            for (int a = 0; a < ndim; ++a)
                for (int b2 = 0; b2 < ndim; ++b2) if (a != b2)
                    for (int da : {-2,2}) for (int db : {-1,1}) {
                        std::vector<int> cur = from;
                        cur[a] += da; cur[b2] += db;
                        if (inBounds(cur, bsize)) squares.push_back(cur);
                    }
            break;
        case 'K': {
            for (int ax = 0; ax < ndim; ++ax)
                for (int d : {-1,1}) {
                    std::vector<int> cur = from; cur[ax] += d;
                    if (inBounds(cur, bsize)) squares.push_back(cur);
                }
            int pairs = 0;
            for (int a = 0; a < ndim && pairs < MAX_K_PAIRS; ++a)
                for (int b2 = a+1; b2 < ndim && pairs < MAX_K_PAIRS; ++b2, ++pairs)
                    for (int da : {-1,1}) for (int db : {-1,1}) {
                        std::vector<int> cur = from; cur[a] += da; cur[b2] += db;
                        if (inBounds(cur, bsize)) squares.push_back(cur);
                    }
            break;
        }
        case 'P':
            for (int ax = 0; ax < ndim; ++ax)
                for (int d : {-1,1}) {
                    std::vector<int> cur = from; cur[ax] += d;
                    if (inBounds(cur, bsize)) squares.push_back(cur);
                }
            break;
    }
    return squares;
}

// ─────────────────────────────────────────────────────────────────────────────
// Puzzle generator
// ─────────────────────────────────────────────────────────────────────────────

struct DiffConfig {
    int  minPieces, maxPieces;
    std::string types;
    bool requireUnique;
    int  bsize(int ndim) const {
        if (types.find('P') != std::string::npos) return 8; // expert
        if (types.find('Q') != std::string::npos) return std::min(8, 6 + ndim/2);
        if (types.find('B') != std::string::npos) return std::min(8, 5 + ndim/2);
        return std::min(8, 4 + ndim/2);
    }
};

static DiffConfig getDiffConfig(const std::string& diff) {
    if (diff == "easy")   return {3,  5,  "RNK",     false};
    if (diff == "hard")   return {7,  11, "RNBQK",   true};
    if (diff == "expert") return {10, 15, "RNBQKP",  true};
    /* medium default */  return {5,  8,  "RNBK",    false};
}

static Board generatePuzzle(int ndim, const std::string& diff,
                              int pieceCount, unsigned seed) {
    DiffConfig cfg = getDiffConfig(diff);
    int bs = cfg.bsize(ndim);
    if (pieceCount <= 0)
        pieceCount = cfg.minPieces + (seed % (cfg.maxPieces - cfg.minPieces + 1));
    pieceCount = std::max(cfg.minPieces, std::min(cfg.maxPieces, pieceCount));

    std::mt19937 rng(seed);

    auto randPos = [&]() {
        std::vector<int> pos(ndim);
        for (int i = 0; i < ndim; ++i)
            pos[i] = rng() % bs;
        return pos;
    };
    auto randType = [&]() -> char {
        return cfg.types[rng() % cfg.types.size()];
    };

    for (int attempt = 0; attempt < 300; ++attempt) {
        Board board;
        board.ndim  = ndim;
        board.bsize = bs;
        // place survivor
        Piece survivor;
        survivor.id   = 0;
        survivor.type = randType();
        survivor.pos  = randPos();
        board.pieces.push_back(survivor);

        bool failed = false;
        for (int n = 1; n < pieceCount; ++n) {
            // pick random existing piece
            int pickIdx = rng() % board.pieces.size();
            const Piece& picker = board.pieces[pickIdx];
            // find squares it can reach on empty board
            auto reach = reachableSquares(picker.type, picker.pos, ndim, bs);
            // filter occupied
            std::vector<std::vector<int>> free;
            for (const auto& sq : reach) {
                bool occ = false;
                for (const auto& ex : board.pieces)
                    if (posEq(ex.pos, sq)) { occ = true; break; }
                if (!occ) free.push_back(sq);
            }
            if (free.empty()) { failed = true; break; }
            std::vector<int> newPos = free[rng() % free.size()];
            Piece np;
            np.id   = n;
            np.type = randType();
            np.pos  = newPos;
            board.pieces.push_back(np);
        }
        if (failed) continue;

        // Validate with solver
        std::vector<Move> solution;
        std::unordered_set<size_t> deadEnds;
        if (!findSolution(board, solution, deadEnds)) continue;

        if (cfg.requireUnique) {
            std::unordered_set<size_t> cache;
            int n = countSolutions(board, 2, cache);
            if (n > 1) continue;
        }

        return board;
    }

    // fallback: return trivial 2-piece board
    Board fb;
    fb.ndim  = ndim;
    fb.bsize = bs;
    fb.pieces.push_back({0, 'R', std::vector<int>(ndim, 0)});
    std::vector<int> p2(ndim, 0); p2[0] = 2;
    fb.pieces.push_back({1, 'R', p2});
    return fb;
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP helpers
// ─────────────────────────────────────────────────────────────────────────────

static const std::string CORS_HEADERS =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n";

static std::string makeResponse(int code, const std::string& body,
                                 const std::string& ctype = "application/json") {
    std::string status;
    if      (code == 200) status = "200 OK";
    else if (code == 400) status = "400 Bad Request";
    else if (code == 404) status = "404 Not Found";
    else if (code == 405) status = "405 Method Not Allowed";
    else                  status = "500 Internal Server Error";

    std::ostringstream ss;
    ss << "HTTP/1.1 " << status << "\r\n"
       << CORS_HEADERS
       << "Content-Type: " << ctype << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Connection: close\r\n\r\n"
       << body;
    return ss.str();
}

static std::string makePreflight() {
    std::ostringstream ss;
    ss << "HTTP/1.1 204 No Content\r\n"
       << CORS_HEADERS
       << "Content-Length: 0\r\n"
       << "Connection: close\r\n\r\n";
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Request handler
// ─────────────────────────────────────────────────────────────────────────────

static std::string handleRequest(const std::string& method,
                                  const std::string& path,
                                  const std::string& body) {
    // OPTIONS preflight
    if (method == "OPTIONS") return makePreflight();

    if (path == "/ping") {
        return makeResponse(200,
            "{\"ok\":true,\"engine\":\"puzzle\",\"version\":\"1.0\"}");
    }

    if (path == "/generate" && method == "POST") {
        int ndim   = parseIntField(body, "ndim",  3);
        std::string diff = parseStrField(body, "difficulty");
        if (diff.empty()) diff = "medium";
        int pc     = parseIntField(body, "piece_count", -1);
        int seedv  = parseIntField(body, "seed", -999);
        if (ndim < 2) ndim = 2;
        if (ndim > MAX_NDIM) ndim = MAX_NDIM;

        unsigned seed = (seedv == -999)
            ? (unsigned)std::chrono::steady_clock::now().time_since_epoch().count()
            : (unsigned)seedv;

        Board b = generatePuzzle(ndim, diff, pc, seed);
        std::ostringstream ss;
        ss << "{\"ok\":true,\"board\":" << boardToJson(b)
           << ",\"seed\":" << seed << "}";
        return makeResponse(200, ss.str());
    }

    if (path == "/legal_captures" && method == "POST") {
        Board b  = parseBoard(body);
        int pidx = parseIntField(body, "piece_id", 0);
        if (pidx < 0 || pidx >= (int)b.pieces.size())
            return makeResponse(400, "{\"ok\":false,\"error\":\"invalid piece_id\"}");
        auto moves = legalCaptures(b, pidx);
        std::ostringstream ss;
        ss << "{\"ok\":true,\"captures\":[";
        for (int i = 0; i < (int)moves.size(); ++i) {
            if (i) ss << ",";
            ss << moveToJson(moves[i]);
        }
        ss << "]}";
        return makeResponse(200, ss.str());
    }

    if (path == "/solve" && method == "POST") {
        Board b = parseBoard(body);
        std::vector<Move> solution;
        std::unordered_set<size_t> deadEnds;
        bool solvable = findSolution(b, solution, deadEnds);
        std::unordered_set<size_t> cache;
        int numSol = solvable ? countSolutions(b, 100, cache) : 0;

        std::ostringstream ss;
        ss << "{\"ok\":true,\"solvable\":" << (solvable ? "true" : "false")
           << ",\"solution\":[";
        for (int i = 0; i < (int)solution.size(); ++i) {
            if (i) ss << ",";
            ss << moveToJson(solution[i]);
        }
        ss << "],\"num_solutions\":" << numSol << "}";
        return makeResponse(200, ss.str());
    }

    return makeResponse(404, "{\"ok\":false,\"error\":\"not found\"}");
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP server
// ─────────────────────────────────────────────────────────────────────────────

static std::string recvAll(SOCKET sock) {
    std::string buf;
    char tmp[4096];
    // read until we have full headers
    while (true) {
        int n = recv(sock, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, n);
        if (buf.find("\r\n\r\n") != std::string::npos) break;
    }
    // parse Content-Length and read body if needed
    auto hend = buf.find("\r\n\r\n");
    if (hend == std::string::npos) return buf;
    std::string headers = buf.substr(0, hend);
    size_t bodyStart    = hend + 4;

    int contentLen = 0;
    auto clPos = headers.find("Content-Length:");
    if (clPos == std::string::npos) clPos = headers.find("content-length:");
    if (clPos != std::string::npos) {
        size_t vp = clPos + 15;
        while (vp < headers.size() && headers[vp] == ' ') ++vp;
        contentLen = std::stoi(headers.substr(vp));
    }

    // read remaining body bytes
    int have = (int)(buf.size() - bodyStart);
    while (have < contentLen) {
        int n = recv(sock, tmp, std::min((int)sizeof(tmp), contentLen - have), 0);
        if (n <= 0) break;
        buf.append(tmp, n);
        have += n;
    }
    return buf;
}

static void parseRequestLine(const std::string& raw,
                              std::string& method, std::string& path) {
    size_t p = raw.find(' ');
    method   = (p != std::string::npos) ? raw.substr(0, p) : "GET";
    size_t q = raw.find(' ', p + 1);
    path     = raw.substr(p + 1, q - p - 1);
}

static void serveClient(SOCKET client) {
    std::string raw = recvAll(client);
    if (raw.empty()) { closesocket(client); return; }

    // split request line / headers / body
    auto lineEnd = raw.find("\r\n");
    std::string reqLine = (lineEnd != std::string::npos) ? raw.substr(0, lineEnd) : raw;
    std::string method, path;
    parseRequestLine(reqLine, method, path);

    // extract body after \r\n\r\n
    std::string body;
    auto hend = raw.find("\r\n\r\n");
    if (hend != std::string::npos) body = raw.substr(hend + 4);

    std::string resp = handleRequest(method, path, body);
    send(client, resp.c_str(), (int)resp.size(), 0);
    closesocket(client);
}

int main(int argc, char* argv[]) {
    int port = 8770;
    if (argc > 1) port = std::atoi(argv[1]);

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n"; return 1;
    }
#endif

    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) {
        std::cerr << "socket() failed\n"; return 1;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((unsigned short)port);

    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed on port " << port << "\n"; return 1;
    }
    if (listen(srv, 16) == SOCKET_ERROR) {
        std::cerr << "listen() failed\n"; return 1;
    }

    std::cout << "Puzzle engine listening on port " << port << "\n";
    std::cout << "Endpoints: /ping  /generate  /legal_captures  /solve\n";

    while (true) {
        sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        SOCKET client  = accept(srv, (sockaddr*)&caddr, &clen);
        if (client == INVALID_SOCKET) continue;
        serveClient(client);
    }

#ifdef _WIN32
    closesocket(srv);
    WSACleanup();
#else
    closesocket(srv);
#endif
    return 0;
}
