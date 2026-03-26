#include "eightd/engine.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace eightd {

namespace {

constexpr int kBoardSize = 8;
constexpr int kBackRankWidth = 8;
constexpr PieceType kBackRank[kBackRankWidth] = {
    PieceType::Rook,
    PieceType::Knight,
    PieceType::Bishop,
    PieceType::Queen,
    PieceType::King,
    PieceType::Bishop,
    PieceType::Knight,
    PieceType::Rook,
};

std::string json_escape(std::string_view text) {
    std::ostringstream output;
    for (char ch : text) {
        switch (ch) {
            case '\\': output << "\\\\"; break;
            case '"': output << "\\\""; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default: output << ch; break;
        }
    }
    return output.str();
}

std::string json_array(const Coordinate& coordinate) {
    std::ostringstream output;
    output << "[";
    for (std::size_t index = 0; index < coordinate.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << coordinate[index];
    }
    output << "]";
    return output.str();
}

std::string piece_json(const Coordinate& coordinate, const Piece& piece) {
    std::ostringstream output;
    output << "{";
    output << "\"coord\":" << json_array(coordinate) << ",";
    output << "\"owner\":" << piece.owner << ",";
    output << "\"type\":\"" << piece_type_name(piece.type) << "\",";
    output << "\"moveCount\":" << piece.move_count;
    if (piece.type == PieceType::Pawn) {
        output << ",\"pawnDirection\":" << json_array(piece.pawn_direction);
        output << ",\"originalOwner\":" << piece.original_owner;
    }
    output << "}";
    return output.str();
}

std::string move_json(const Move& move) {
    std::ostringstream output;
    output << "{";
    output << "\"from\":" << json_array(move.from) << ",";
    output << "\"to\":" << json_array(move.to) << ",";
    output << "\"capture\":" << (move.is_capture ? "true" : "false") << ",";
    output << "\"capturesKing\":" << (move.captures_king ? "true" : "false") << ",";
    output << "\"promotion\":" << (move.promotion ? "true" : "false");
    if (move.captured_piece.has_value()) {
        output << ",\"capturedType\":\"" << piece_type_name(move.captured_piece->type) << "\"";
        output << ",\"capturedOwner\":" << move.captured_piece->owner;
    }
    if (move.promotion_type.has_value()) {
        output << ",\"promotionType\":\"" << piece_type_name(*move.promotion_type) << "\"";
    }
    output << "}";
    return output.str();
}

std::string json_bool(bool value) {
    return value ? "true" : "false";
}

template <typename T>
std::string json_number_map(const std::map<int, T>& values) {
    std::ostringstream output;
    output << "{";
    bool first = true;
    for (const auto& [key, value] : values) {
        if (!first) {
            output << ",";
        }
        first = false;
        output << "\"" << key << "\":" << value;
    }
    output << "}";
    return output.str();
}

std::string iso_timestamp_now() {
    const std::time_t now = std::time(nullptr);
    std::tm utc {};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::string snapshot_label(int move_count) {
    if (move_count < 40) {
        return "Opening";
    }
    if (move_count < 200) {
        return "Midgame";
    }
    return "Endgame";
}

std::string personality_json(const PersonalityProfile& personality) {
    std::ostringstream output;
    output << "{"
           << "\"label\":\"" << json_escape(personality.label) << "\","
           << "\"honesty\":" << personality.honesty << ","
           << "\"cooperation\":" << personality.cooperation << ","
           << "\"prosocial\":" << personality.prosocial << ","
           << "\"trusting\":" << personality.trusting << ","
           << "\"communicative\":" << personality.communicative << ","
           << "\"aggression\":" << personality.aggression << ","
           << "\"foresight\":" << personality.foresight << ","
           << "\"defensiveness\":" << personality.defensiveness << ","
           << "\"patience\":" << personality.patience << ","
           << "\"regicide\":" << personality.regicide << ","
           << "\"vendetta\":" << personality.vendetta << ","
           << "\"greed\":" << personality.greed << ","
           << "\"chaos\":" << personality.chaos << ","
           << "\"attention\":" << personality.attention << ","
           << "\"allianceComfort\":" << personality.alliance_comfort
           << "}";
    return output.str();
}

std::string checkpoint_json(const ResearchCheckpoint& checkpoint) {
    std::ostringstream output;
    output << "{"
           << "\"move\":" << checkpoint.move << ","
           << "\"playersRemaining\":" << checkpoint.players_remaining << ","
           << "\"leader\":";
    if (checkpoint.leader.has_value()) {
        output << *checkpoint.leader;
    } else {
        output << "null";
    }
    output << ",\"pieceCounts\":" << json_number_map(checkpoint.piece_counts) << ",\"alliances\":[";
    for (std::size_t index = 0; index < checkpoint.alliances.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << "[";
        for (std::size_t j = 0; j < checkpoint.alliances[index].size(); ++j) {
            if (j != 0) {
                output << ",";
            }
            output << checkpoint.alliances[index][j];
        }
        output << "]";
    }
    output << "],\"state\":";
    if (!checkpoint.state_json.empty()) {
        output << checkpoint.state_json;
    } else {
        output << "null";
    }
    output << "}";
    return output.str();
}

std::string chat_message_json(const ChatMessage& message) {
    std::ostringstream output;
    output << "{"
           << "\"index\":" << message.index << ","
           << "\"turn\":" << message.turn << ","
           << "\"sender\":" << message.sender << ","
           << "\"recipients\":[";
    for (std::size_t index = 0; index < message.recipients.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << message.recipients[index];
    }
    output << "],"
           << "\"public\":" << json_bool(message.is_public) << ","
           << "\"mode\":\"" << json_escape(message.mode) << "\","
           << "\"book\":\"" << json_escape(message.book) << "\","
           << "\"content\":\"" << json_escape(message.content) << "\","
           << "\"target\":";
    if (message.target.has_value()) {
        output << *message.target;
    } else {
        output << "null";
    }
    output << "}";
    return output.str();
}

std::string replay_event_json(const ReplayEvent& event) {
    std::ostringstream output;
    output << "{"
           << "\"move\":" << event.move << ","
           << "\"type\":\"" << json_escape(event.type) << "\","
           << "\"label\":\"" << json_escape(event.label) << "\","
           << "\"actor\":";
    if (event.actor.has_value()) {
        output << *event.actor;
    } else {
        output << "null";
    }
    output << ",\"target\":";
    if (event.target.has_value()) {
        output << *event.target;
    } else {
        output << "null";
    }
    output << "}";
    return output.str();
}

std::string replay_segment_json(const ReplaySegment& segment) {
    std::ostringstream output;
    output << "{"
           << "\"start_move\":" << segment.start_move << ","
           << "\"end_move\":" << segment.end_move << ","
           << "\"start_state\":";
    if (!segment.start_state_json.empty()) {
        output << segment.start_state_json;
    } else {
        output << "null";
    }
    output << ",\"move_log\":[";
    for (std::size_t index = 0; index < segment.move_log.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << "\"" << json_escape(segment.move_log[index]) << "\"";
    }
    output << "],\"chat_log\":[";
    for (std::size_t index = 0; index < segment.chat_log.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << chat_message_json(segment.chat_log[index]);
    }
    output << "],\"replay_events\":[";
    for (std::size_t index = 0; index < segment.replay_events.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << replay_event_json(segment.replay_events[index]);
    }
    output << "]}";
    return output.str();
}

std::string turn_advice_json(const TurnAdvice& advice) {
    std::ostringstream output;
    output << "{"
           << "\"playerId\":" << advice.player_id << ","
           << "\"hasMove\":" << json_bool(advice.has_move) << ","
           << "\"inCheck\":" << json_bool(advice.in_check) << ","
           << "\"legalMoveCount\":" << advice.legal_move_count << ","
           << "\"kingThreats\":[";
    for (std::size_t index = 0; index < advice.king_threats.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << json_array(advice.king_threats[index]);
    }
    output << "]";
    if (advice.has_move) {
        output << ",\"move\":" << move_json(advice.move);
    } else {
        output << ",\"move\":null";
    }
    output << "}";
    return output.str();
}

std::string champion_json(const ChampionRecord& champion) {
    std::ostringstream output;
    output << "{"
           << "\"rank\":" << champion.rank << ","
           << "\"label\":\"" << json_escape(champion.label) << "\","
           << "\"aggression\":" << champion.aggression << ","
           << "\"foresight\":" << champion.foresight << ","
           << "\"defensiveness\":" << champion.defensiveness << ","
           << "\"patience\":" << champion.patience << ","
           << "\"regicide\":" << champion.regicide << ","
           << "\"vendetta\":" << champion.vendetta << ","
           << "\"greed\":" << champion.greed << ","
           << "\"chaos\":" << champion.chaos << ","
           << "\"attention\":" << champion.attention << ","
           << "\"allianceComfort\":" << champion.alliance_comfort << ","
           << "\"fitness\":" << champion.fitness << ","
           << "\"avgMoveMs\":" << champion.avg_move_ms << ","
           << "\"avgPower\":" << champion.avg_power << ","
           << "\"speedGrade\":\"" << json_escape(champion.speed_grade) << "\","
           << "\"powerGrade\":\"" << json_escape(champion.power_grade) << "\""
           << "}";
    return output.str();
}

}  // namespace

std::string piece_type_name(PieceType type) {
    switch (type) {
        case PieceType::King: return "king";
        case PieceType::Queen: return "queen";
        case PieceType::Rook: return "rook";
        case PieceType::Bishop: return "bishop";
        case PieceType::Knight: return "knight";
        case PieceType::Pawn: return "pawn";
    }
    return "unknown";
}

char piece_type_symbol(PieceType type) {
    switch (type) {
        case PieceType::King: return 'K';
        case PieceType::Queen: return 'Q';
        case PieceType::Rook: return 'R';
        case PieceType::Bishop: return 'B';
        case PieceType::Knight: return 'N';
        case PieceType::Pawn: return 'P';
    }
    return '?';
}

Coordinate parse_coordinate(std::string_view text) {
    Coordinate coordinate;
    std::stringstream stream{std::string(text)};
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) {
            coordinate.push_back(std::stoi(token));
        }
    }
    return coordinate;
}

SliceRequest parse_slice_request(std::string_view view_text, std::string_view fixed_text) {
    SliceRequest request;
    request.view_axes = parse_coordinate(view_text);

    std::stringstream stream{std::string(fixed_text)};
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (token.empty()) {
            continue;
        }
        const auto separator = token.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        request.fixed_axes[std::stoi(token.substr(0, separator))] = std::stoi(token.substr(separator + 1));
    }

    return request;
}

GameState::GameState(int dimensions) {
    reset(dimensions);
}

void GameState::reset(int dimensions) {
    if (dimensions < 2 || dimensions > 8) {
        throw std::invalid_argument("dimensions must be between 2 and 8");
    }

    dimensions_ = dimensions;
    board_size_ = kBoardSize;
    player_count_ = 1 << (dimensions_ - 1);
    current_player_ = 0;
    move_number_ = 0;
    game_over_ = false;
    winner_.reset();
    pieces_.clear();
    log_.clear();
    en_passant_target_.reset();
    en_passant_victim_square_.reset();
    en_passant_owner_.reset();
    pieces_.reserve(static_cast<std::size_t>(player_count_) * static_cast<std::size_t>(kBackRankWidth) * static_cast<std::size_t>(dimensions_));
    active_players_.clear();
    active_players_.reserve(player_count_);
    player_piece_cache_.assign(static_cast<std::size_t>(player_count_), {});
    piece_count_cache_.assign(static_cast<std::size_t>(player_count_), 0);
    king_square_cache_.assign(static_cast<std::size_t>(player_count_), std::nullopt);
    for (int player_id = 0; player_id < player_count_; ++player_id) {
        active_players_.push_back(player_id);
    }
    place_initial_pieces();
}

void GameState::clear_for_testing() {
    pieces_.clear();
    log_.clear();
    game_over_ = false;
    winner_.reset();
    move_number_ = 0;
    en_passant_target_.reset();
    en_passant_victim_square_.reset();
    en_passant_owner_.reset();
    active_players_.clear();
    for (int player_id = 0; player_id < player_count_; ++player_id) {
        active_players_.push_back(player_id);
    }
    player_piece_cache_.assign(static_cast<std::size_t>(player_count_), {});
    piece_count_cache_.assign(static_cast<std::size_t>(player_count_), 0);
    king_square_cache_.assign(static_cast<std::size_t>(player_count_), std::nullopt);
}

void GameState::set_current_player(int player_id) {
    current_player_ = player_id;
}

void GameState::set_piece(const Coordinate& coordinate, const Piece& piece) {
    pieces_[key_for(coordinate)] = piece;
    rebuild_piece_cache();
}

Coordinate GameState::player_home(int player_id) const {
    Coordinate home(dimensions_, 0);
    for (int axis = 1; axis < dimensions_; ++axis) {
        home[axis] = ((player_id >> (axis - 1)) & 1) == 0 ? 0 : board_size_ - 1;
    }
    return home;
}

std::string GameState::key_for(const Coordinate& coordinate) const {
    std::ostringstream output;
    for (int axis = 0; axis < dimensions_; ++axis) {
        if (axis != 0) {
            output << ",";
        }
        output << coordinate.at(axis);
    }
    return output.str();
}

std::optional<Piece> GameState::piece_at(const Coordinate& coordinate) const {
    const auto iterator = pieces_.find(key_for(coordinate));
    if (iterator == pieces_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

bool GameState::in_bounds(const Coordinate& coordinate) const {
    if (static_cast<int>(coordinate.size()) != dimensions_) {
        return false;
    }
    return std::all_of(coordinate.begin(), coordinate.end(), [&](int value) {
        return value >= 0 && value < board_size_;
    });
}

void GameState::place_initial_pieces() {
    for (int player_id = 0; player_id < player_count_; ++player_id) {
        const Coordinate home = player_home(player_id);
        place_back_rank(player_id, home);
        place_pawns(player_id, home);
    }
    rebuild_piece_cache();
    log_.push_back("Game initialized");
}

void GameState::place_back_rank(int player_id, const Coordinate& home) {
    for (int file = 0; file < kBackRankWidth; ++file) {
        Coordinate coordinate = home;
        coordinate[0] = file;
        pieces_[key_for(coordinate)] = Piece{.type = kBackRank[file], .owner = player_id, .pawn_direction = {}, .original_owner = player_id};
    }
}

void GameState::place_pawns(int player_id, const Coordinate& home) {
    std::unordered_set<std::string> seen_directions;
    seen_directions.reserve(static_cast<std::size_t>((std::max)(1, player_count_ - 1)));
    for (int opponent = 0; opponent < player_count_; ++opponent) {
        if (opponent == player_id) {
            continue;
        }
        Coordinate direction(dimensions_, 0);
        const Coordinate other_home = player_home(opponent);
        for (int axis = 1; axis < dimensions_; ++axis) {
            if (other_home[axis] > home[axis]) {
                direction[axis] = 1;
            } else if (other_home[axis] < home[axis]) {
                direction[axis] = -1;
            } else {
                direction[axis] = 0;
            }
        }

        const std::string direction_key = key_for(direction);
        if (!seen_directions.insert(direction_key).second) {
            continue;
        }

        for (int file = 0; file < kBackRankWidth; ++file) {
            Coordinate coordinate = home;
            coordinate[0] = file;
            for (int axis = 1; axis < dimensions_; ++axis) {
                coordinate[axis] += direction[axis];
            }
            if (!in_bounds(coordinate)) {
                continue;
            }
            const std::string coordinate_key = key_for(coordinate);
            if (pieces_.contains(coordinate_key)) {
                continue;
            }
            pieces_[coordinate_key] = Piece{
                .type = PieceType::Pawn,
                .owner = player_id,
                .pawn_direction = direction,
                .original_owner = player_id,
            };
        }
    }
}

std::vector<Coordinate> GameState::piece_coordinates_for_player(int player_id) const {
    std::vector<Coordinate> coordinates;
    if (player_id < 0 || player_id >= player_count_) {
        return coordinates;
    }
    const auto& cached = player_piece_cache_[static_cast<std::size_t>(player_id)];
    coordinates.reserve(cached.size());
    for (const auto& entry : cached) {
        coordinates.push_back(entry.first);
    }
    return coordinates;
}

bool GameState::path_clear(const Coordinate& from, const Coordinate& to, const Coordinate& step) const {
    Coordinate current = from;
    for (;;) {
        for (int axis = 0; axis < dimensions_; ++axis) {
            current[axis] += step[axis];
        }
        if (current == to) {
            return true;
        }
        if (piece_at(current).has_value()) {
            return false;
        }
    }
}

bool GameState::piece_attacks_square(const Coordinate& from, const Piece& piece, const Coordinate& target) const {
    if (from == target) {
        return false;
    }

    Coordinate diff(dimensions_, 0);
    for (int axis = 0; axis < dimensions_; ++axis) {
        diff[axis] = target[axis] - from[axis];
    }

    if (piece.type == PieceType::Pawn) {
        Coordinate forward = from;
        for (int axis = 0; axis < dimensions_; ++axis) {
            forward[axis] += piece.pawn_direction[axis];
        }
        for (int axis = 0; axis < dimensions_; ++axis) {
            if (piece.pawn_direction[axis] != 0) {
                continue;
            }
            for (const int lateral : {-1, 1}) {
                Coordinate capture = forward;
                capture[axis] += lateral;
                if (capture == target) {
                    return true;
                }
            }
        }
        return false;
    }

    if (piece.type == PieceType::Knight) {
        int two_count = 0;
        int one_count = 0;
        for (int axis = 0; axis < dimensions_; ++axis) {
            const int delta = std::abs(diff[axis]);
            if (delta == 2) {
                ++two_count;
            } else if (delta == 1) {
                ++one_count;
            } else if (delta != 0) {
                return false;
            }
        }
        return two_count == 1 && one_count == 1;
    }

    if (piece.type == PieceType::King) {
        int changed = 0;
        for (int axis = 0; axis < dimensions_; ++axis) {
            const int delta = std::abs(diff[axis]);
            if (delta > 1) {
                return false;
            }
            if (delta == 1) {
                ++changed;
            }
        }
        return changed >= 1 && changed <= 2;
    }

    const auto sign = [](int value) { return value == 0 ? 0 : (value > 0 ? 1 : -1); };

    if (piece.type == PieceType::Rook || piece.type == PieceType::Queen) {
        int changed_axis = -1;
        for (int axis = 0; axis < dimensions_; ++axis) {
            if (diff[axis] != 0) {
                if (changed_axis != -1) {
                    changed_axis = -2;
                    break;
                }
                changed_axis = axis;
            }
        }
        if (changed_axis >= 0) {
            Coordinate step(dimensions_, 0);
            step[changed_axis] = sign(diff[changed_axis]);
            if (path_clear(from, target, step)) {
                return true;
            }
        }
    }

    if (piece.type == PieceType::Bishop || piece.type == PieceType::Queen) {
        int changed = 0;
        int magnitude = 0;
        Coordinate step(dimensions_, 0);
        bool valid = true;
        for (int axis = 0; axis < dimensions_; ++axis) {
            if (diff[axis] == 0) {
                continue;
            }
            const int delta = std::abs(diff[axis]);
            if (magnitude == 0) {
                magnitude = delta;
            } else if (delta != magnitude) {
                valid = false;
                break;
            }
            step[axis] = sign(diff[axis]);
            ++changed;
        }
        if (valid && changed == 2 && path_clear(from, target, step)) {
            return true;
        }
    }

    return false;
}

bool GameState::square_attacked_by(int attacker_id, const Coordinate& target) const {
    if (attacker_id < 0 || attacker_id >= player_count_) {
        return false;
    }
    for (const auto& entry : player_piece_cache_[static_cast<std::size_t>(attacker_id)]) {
        if (piece_attacks_square(entry.first, entry.second, target)) {
            return true;
        }
    }
    return false;
}

std::vector<Move> GameState::pseudo_legal_moves_from(const Coordinate& from) const {
    std::vector<Move> moves;
    if (!in_bounds(from)) {
        return moves;
    }

    const std::optional<Piece> piece = piece_at(from);
    if (!piece.has_value()) {
        return moves;
    }

    const auto add_move = [&](const Coordinate& to) {
        if (!in_bounds(to)) {
            return;
        }
        const std::optional<Piece> target = piece_at(to);
        if (target.has_value() && target->owner == piece->owner) {
            return;
        }
        Move move;
        move.from = from;
        move.to = to;
        move.is_capture = target.has_value();
        move.captured_piece = target;
        move.captures_king = target.has_value() && target->type == PieceType::King;
        moves.push_back(move);
    };

    const auto slide = [&](const Coordinate& direction) {
        Coordinate current = from;
        for (;;) {
            for (int axis = 0; axis < dimensions_; ++axis) {
                current[axis] += direction[axis];
            }
            if (!in_bounds(current)) {
                break;
            }
            const std::optional<Piece> target = piece_at(current);
            if (!target.has_value()) {
                Move move;
                move.from = from;
                move.to = current;
                moves.push_back(move);
                continue;
            }
            if (target->owner != piece->owner) {
                Move move;
                move.from = from;
                move.to = current;
                move.is_capture = true;
                move.captured_piece = target;
                move.captures_king = target->type == PieceType::King;
                moves.push_back(move);
            }
            break;
        }
    };

    if (piece->type == PieceType::Knight) {
        for (int axis_a = 0; axis_a < dimensions_; ++axis_a) {
            for (int axis_b = 0; axis_b < dimensions_; ++axis_b) {
                if (axis_a == axis_b) {
                    continue;
                }
                for (const int long_step : {2, -2}) {
                    for (const int short_step : {1, -1}) {
                        Coordinate to = from;
                        to[axis_a] += long_step;
                        to[axis_b] += short_step;
                        add_move(to);
                    }
                }
            }
        }
    } else if (piece->type == PieceType::King) {
        for (int axis = 0; axis < dimensions_; ++axis) {
            for (const int step : {-1, 1}) {
                Coordinate to = from;
                to[axis] += step;
                add_move(to);
            }
        }
        for (int axis_a = 0; axis_a < dimensions_; ++axis_a) {
            for (int axis_b = axis_a + 1; axis_b < dimensions_; ++axis_b) {
                for (const int step_a : {-1, 1}) {
                    for (const int step_b : {-1, 1}) {
                        Coordinate to = from;
                        to[axis_a] += step_a;
                        to[axis_b] += step_b;
                        add_move(to);
                    }
                }
            }
        }

        if (piece->move_count == 0 && !is_in_check(piece->owner)) {
            const Coordinate home = player_home(piece->owner);
            Coordinate expected_king = home;
            expected_king[0] = 4;
            if (from == expected_king) {
                for (const int rook_file : {0, 7}) {
                    Coordinate rook_coord = home;
                    rook_coord[0] = rook_file;
                    const std::optional<Piece> rook = piece_at(rook_coord);
                    if (!rook.has_value() || rook->owner != piece->owner || rook->type != PieceType::Rook || rook->move_count != 0) {
                        continue;
                    }
                    const int direction = rook_file == 0 ? -1 : 1;
                    const int king_target_file = rook_file == 0 ? 2 : 6;
                    const int rook_target_file = rook_file == 0 ? 3 : 5;
                    bool clear = true;
                    for (int file = std::min(from[0], rook_coord[0]) + 1; file < std::max(from[0], rook_coord[0]); ++file) {
                        Coordinate between = home;
                        between[0] = file;
                        if (piece_at(between).has_value()) {
                            clear = false;
                            break;
                        }
                    }
                    if (!clear) {
                        continue;
                    }
                    Coordinate transit = home;
                    transit[0] = from[0] + direction;
                    Coordinate target = home;
                    target[0] = king_target_file;
                    if (piece_at(transit).has_value() || piece_at(target).has_value()) {
                        continue;
                    }
                    bool attacked = false;
                    for (int enemy = 0; enemy < player_count_; ++enemy) {
                        if (enemy == piece->owner || std::find(active_players_.begin(), active_players_.end(), enemy) == active_players_.end()) {
                            continue;
                        }
                        if (square_attacked_by(enemy, transit) || square_attacked_by(enemy, target)) {
                            attacked = true;
                            break;
                        }
                    }
                    if (attacked) {
                        continue;
                    }
                    Move castle;
                    castle.from = from;
                    castle.to = target;
                    castle.is_castle = true;
                    castle.rook_from = rook_coord;
                    Coordinate rook_target = home;
                    rook_target[0] = rook_target_file;
                    castle.rook_to = rook_target;
                    moves.push_back(castle);
                }
            }
        }
    } else if (piece->type == PieceType::Pawn) {
        Coordinate forward = from;
        for (int axis = 0; axis < dimensions_; ++axis) {
            forward[axis] += piece->pawn_direction[axis];
        }
        if (in_bounds(forward) && !piece_at(forward).has_value()) {
            Move quiet;
            quiet.from = from;
            quiet.to = forward;
            moves.push_back(quiet);

            Coordinate start = player_home(piece->original_owner);
            start[0] = from[0];
            for (int axis = 1; axis < dimensions_; ++axis) {
                start[axis] += piece->pawn_direction[axis];
            }
            if (start == from && piece->move_count == 0) {
                Coordinate double_forward = from;
                for (int axis = 0; axis < dimensions_; ++axis) {
                    double_forward[axis] += piece->pawn_direction[axis] * 2;
                }
                if (in_bounds(double_forward) && !piece_at(double_forward).has_value()) {
                    Move double_move;
                    double_move.from = from;
                    double_move.to = double_forward;
                    moves.push_back(double_move);
                }
            }
        }

        for (int axis = 0; axis < dimensions_; ++axis) {
            if (piece->pawn_direction[axis] != 0) {
                continue;
            }
            for (const int lateral : {-1, 1}) {
                Coordinate capture = forward;
                capture[axis] += lateral;
                if (!in_bounds(capture)) {
                    continue;
                }
                const std::optional<Piece> target = piece_at(capture);
                if (target.has_value() && target->owner != piece->owner) {
                    Move move;
                    move.from = from;
                    move.to = capture;
                    move.is_capture = true;
                    move.captured_piece = target;
                    move.captures_king = target->type == PieceType::King;
                    moves.push_back(move);
                }
                if (en_passant_target_.has_value() && capture == *en_passant_target_ &&
                    en_passant_owner_.has_value() && *en_passant_owner_ != piece->owner &&
                    en_passant_victim_square_.has_value()) {
                    const std::optional<Piece> victim = piece_at(*en_passant_victim_square_);
                    if (victim.has_value() && victim->owner != piece->owner && victim->type == PieceType::Pawn) {
                        Move move;
                        move.from = from;
                        move.to = capture;
                        move.is_capture = true;
                        move.captured_piece = victim;
                        move.is_en_passant = true;
                        move.en_passant_capture_square = *en_passant_victim_square_;
                        moves.push_back(move);
                    }
                }
            }
        }
    } else {
        const bool axial = piece->type == PieceType::Rook || piece->type == PieceType::Queen;
        const bool diagonal = piece->type == PieceType::Bishop || piece->type == PieceType::Queen;
        if (axial) {
            for (int axis = 0; axis < dimensions_; ++axis) {
                for (const int step : {-1, 1}) {
                    Coordinate direction(dimensions_, 0);
                    direction[axis] = step;
                    slide(direction);
                }
            }
        }
        if (diagonal) {
            for (int axis_a = 0; axis_a < dimensions_; ++axis_a) {
                for (int axis_b = axis_a + 1; axis_b < dimensions_; ++axis_b) {
                    for (const int step_a : {-1, 1}) {
                        for (const int step_b : {-1, 1}) {
                            Coordinate direction(dimensions_, 0);
                            direction[axis_a] = step_a;
                            direction[axis_b] = step_b;
                            slide(direction);
                        }
                    }
                }
            }
        }
    }

    for (Move& move : moves) {
        if (piece->type != PieceType::Pawn) {
            continue;
        }
        bool at_end = true;
        for (int axis = 1; axis < dimensions_; ++axis) {
            const int direction = piece->pawn_direction[axis];
            if (direction == 0) {
                continue;
            }
            const int expected = direction > 0 ? board_size_ - 1 : 0;
            if (move.to[axis] != expected) {
                at_end = false;
                break;
            }
        }
        if (!at_end) {
            continue;
        }
        const bool has_queen = std::any_of(pieces_.begin(), pieces_.end(), [&](const auto& entry) {
            return entry.second.owner == piece->owner && entry.second.type == PieceType::Queen;
        });
        move.promotion = true;
        move.promotion_type = has_queen ? PieceType::Knight : PieceType::Queen;
    }

    return moves;
}

std::vector<Move> GameState::legal_moves_from(const Coordinate& from) const {
    const std::optional<Piece> piece = piece_at(from);
    if (!piece.has_value()) {
        return {};
    }

    std::vector<Move> legal;
    for (const Move& move : pseudo_legal_moves_from(from)) {
        GameState trial = *this;
        trial.apply_move_unchecked(move, false, false);
        if (!trial.is_in_check(piece->owner)) {
            legal.push_back(move);
        }
    }
    return legal;
}

std::vector<Move> GameState::legal_moves_for_player(int player_id) const {
    std::vector<Move> all_moves;
    for (const Coordinate& coordinate : piece_coordinates_for_player(player_id)) {
        std::vector<Move> local = legal_moves_from(coordinate);
        all_moves.insert(all_moves.end(), local.begin(), local.end());
    }
    return all_moves;
}

std::vector<Move> GameState::candidate_moves_for_player(int player_id, std::size_t per_piece_limit, std::size_t total_limit) const {
    std::vector<Move> candidates;
    if (player_id < 0 || player_id >= player_count_) {
        return candidates;
    }
    const auto& pieces = player_piece_cache_[static_cast<std::size_t>(player_id)];
    std::vector<std::size_t> piece_order(pieces.size());
    for (std::size_t index = 0; index < pieces.size(); ++index) {
        piece_order[index] = index;
    }
    auto piece_priority = [&](const Piece& piece) {
        switch (piece.type) {
            case PieceType::King: return 120.0;
            case PieceType::Knight: return 95.0;
            case PieceType::Pawn: return 90.0;
            case PieceType::Queen: return 75.0;
            case PieceType::Rook: return 65.0;
            case PieceType::Bishop: return 55.0;
        }
        return 0.0;
    };
    const std::size_t piece_budget = dimensions_ >= 8 ? (std::min)(pieces.size(), static_cast<std::size_t>(18))
                                 : dimensions_ >= 7 ? (std::min)(pieces.size(), static_cast<std::size_t>(28))
                                                    : pieces.size();
    if (piece_budget < piece_order.size()) {
        std::partial_sort(piece_order.begin(),
                          piece_order.begin() + static_cast<std::ptrdiff_t>(piece_budget),
                          piece_order.end(),
                          [&](std::size_t left, std::size_t right) {
                              const auto& lhs = pieces[left];
                              const auto& rhs = pieces[right];
                              double lscore = piece_priority(lhs.second);
                              double rscore = piece_priority(rhs.second);
                              for (int value : lhs.first) lscore -= std::abs(value - 3.5) * 0.03;
                              for (int value : rhs.first) rscore -= std::abs(value - 3.5) * 0.03;
                              return lscore > rscore;
                          });
        piece_order.resize(piece_budget);
    }
    auto move_priority = [&](const Move& move, const Piece& piece) {
        double score = 0.0;
        if (move.captures_king) score += 100000.0;
        if (move.promotion) score += 5000.0;
        if (move.is_capture && move.captured_piece.has_value()) {
            switch (move.captured_piece->type) {
                case PieceType::King: score += 50000.0; break;
                case PieceType::Queen: score += 900.0; break;
                case PieceType::Rook: score += 500.0; break;
                case PieceType::Bishop: score += 300.0; break;
                case PieceType::Knight: score += 280.0; break;
                case PieceType::Pawn: score += 120.0; break;
            }
        }
        switch (piece.type) {
            case PieceType::King: score += 30.0; break;
            case PieceType::Queen: score += 18.0; break;
            case PieceType::Rook: score += 15.0; break;
            case PieceType::Bishop: score += 12.0; break;
            case PieceType::Knight: score += 14.0; break;
            case PieceType::Pawn: score += 10.0; break;
        }
        for (int value : move.to) {
            score -= std::abs(value - 3.5) * 0.22;
        }
        return score;
    };

    for (std::size_t ordered_index : piece_order) {
        const auto& entry = pieces[ordered_index];
        const auto pseudo = pseudo_legal_moves_from(entry.first);
        if (pseudo.empty()) {
            continue;
        }
        std::vector<std::pair<double, Move>> ranked;
        ranked.reserve(pseudo.size());
        for (const Move& move : pseudo) {
            ranked.emplace_back(move_priority(move, entry.second), move);
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
            return left.first > right.first;
        });
        const std::size_t keep = (std::min)(per_piece_limit, ranked.size());
        for (std::size_t index = 0; index < keep; ++index) {
            candidates.push_back(ranked[index].second);
        }
        if (candidates.size() >= total_limit) {
            break;
        }
    }

    if (candidates.size() > total_limit) {
        candidates.resize(total_limit);
    }
    return candidates;
}

bool GameState::has_any_legal_move_for_player(int player_id) const {
    for (const Coordinate& coordinate : piece_coordinates_for_player(player_id)) {
        const std::optional<Piece> piece = piece_at(coordinate);
        if (!piece.has_value()) {
            continue;
        }
        for (const Move& move : pseudo_legal_moves_from(coordinate)) {
            if (move_keeps_king_safe(move, piece->owner)) {
                return true;
            }
        }
    }
    return false;
}

bool GameState::move_keeps_king_safe(const Move& move, int player_id) const {
    GameState trial = *this;
    trial.apply_move_unchecked(move, false, false);
    return !trial.is_in_check(player_id);
}

bool GameState::has_king(int player_id) const {
    return player_id >= 0 && player_id < player_count_ &&
           king_square_cache_[static_cast<std::size_t>(player_id)].has_value();
}

int GameState::piece_count(int player_id) const {
    if (player_id < 0 || player_id >= player_count_) {
        return 0;
    }
    return piece_count_cache_[static_cast<std::size_t>(player_id)];
}

bool GameState::is_in_check(int player_id) const {
    if (player_id < 0 || player_id >= player_count_) {
        return false;
    }
    const auto& king_square = king_square_cache_[static_cast<std::size_t>(player_id)];
    if (!king_square.has_value()) {
        return false;
    }
    for (int enemy = 0; enemy < player_count_; ++enemy) {
        if (enemy == player_id) {
            continue;
        }
        if (std::find(active_players_.begin(), active_players_.end(), enemy) == active_players_.end()) {
            continue;
        }
        if (square_attacked_by(enemy, *king_square)) {
            return true;
        }
    }
    return false;
}

std::vector<Coordinate> GameState::king_threats(int player_id) const {
    if (player_id < 0 || player_id >= player_count_) {
        return {};
    }
    const auto& king_square = king_square_cache_[static_cast<std::size_t>(player_id)];
    if (!king_square.has_value()) {
        return {};
    }

    std::vector<Coordinate> threats;
    for (int enemy = 0; enemy < player_count_; ++enemy) {
        if (enemy == player_id) {
            continue;
        }
        if (std::find(active_players_.begin(), active_players_.end(), enemy) == active_players_.end()) {
            continue;
        }
        for (const auto& entry : player_piece_cache_[static_cast<std::size_t>(enemy)]) {
            if (piece_attacks_square(entry.first, entry.second, *king_square)) {
                threats.push_back(entry.first);
            }
        }
    }
    return threats;
}

std::vector<std::pair<Coordinate, Piece>> GameState::slice_view(const SliceRequest& request) const {
    std::vector<std::pair<Coordinate, Piece>> visible;
    for (const auto& player_entries : player_piece_cache_) {
        for (const auto& entry : player_entries) {
            const Coordinate& coordinate = entry.first;
            bool matches = true;
            for (const auto& [axis, value] : request.fixed_axes) {
                if (axis < 0 || axis >= dimensions_ || coordinate[axis] != value) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                visible.emplace_back(coordinate, entry.second);
            }
        }
    }
    std::sort(visible.begin(), visible.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    return visible;
}

std::string GameState::serialize_state_json(bool include_log, bool include_checks) const {
    std::ostringstream output;
    output << "{";
    output << "\"dimensions\":" << dimensions_ << ",";
    output << "\"boardSize\":" << board_size_ << ",";
    output << "\"playerCount\":" << player_count_ << ",";
    output << "\"currentPlayer\":" << current_player_ << ",";
    output << "\"moveNumber\":" << move_number_ << ",";
    output << "\"gameOver\":" << (game_over_ ? "true" : "false") << ",";
    output << "\"inCheck\":";
    if (include_checks) {
        output << (is_in_check(current_player_) ? "true" : "false");
    } else {
        output << "false";
    }
    output << ",";
    output << "\"winner\":";
    if (winner_.has_value()) {
        output << *winner_;
    } else {
        output << "null";
    }
    output << ",";
    output << "\"activePlayers\":[";
    for (std::size_t index = 0; index < active_players_.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << active_players_[index];
    }
    output << "],";
    output << "\"enPassantTarget\":";
    if (en_passant_target_.has_value()) {
        output << json_array(*en_passant_target_);
    } else {
        output << "null";
    }
    output << ",";
    output << "\"pieces\":[";
    bool first = true;
    for (const auto& player_entries : player_piece_cache_) {
        for (const auto& entry : player_entries) {
            if (!first) {
                output << ",";
            }
            first = false;
            output << piece_json(entry.first, entry.second);
        }
    }
    output << "],";
    output << "\"log\":";
    if (include_log) {
        output << "[";
        for (std::size_t index = 0; index < log_.size(); ++index) {
            if (index != 0) {
                output << ",";
            }
            output << "\"" << json_escape(log_[index]) << "\"";
        }
        output << "]";
    } else {
        output << "[]";
    }
    output << "}";
    return output.str();
}

std::string GameState::serialize_slice_json(const SliceRequest& request) const {
    const auto visible = slice_view(request);
    std::ostringstream output;
    output << "{";
    output << "\"viewAxes\":" << json_array(request.view_axes) << ",";
    output << "\"pieces\":[";
    for (std::size_t index = 0; index < visible.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << piece_json(visible[index].first, visible[index].second);
    }
    output << "]";
    output << "}";
    return output.str();
}

std::string GameState::serialize_moves_json(const std::vector<Move>& moves) const {
    std::ostringstream output;
    output << "[";
    for (std::size_t index = 0; index < moves.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << move_json(moves[index]);
    }
    output << "]";
    return output.str();
}

void GameState::rebuild_piece_cache() {
    player_piece_cache_.assign(static_cast<std::size_t>(player_count_), {});
    piece_count_cache_.assign(static_cast<std::size_t>(player_count_), 0);
    king_square_cache_.assign(static_cast<std::size_t>(player_count_), std::nullopt);
    for (const auto& [key, piece] : pieces_) {
        if (piece.owner < 0 || piece.owner >= player_count_) {
            continue;
        }
        const Coordinate coordinate = parse_coordinate(key);
        auto& bucket = player_piece_cache_[static_cast<std::size_t>(piece.owner)];
        bucket.emplace_back(coordinate, piece);
        piece_count_cache_[static_cast<std::size_t>(piece.owner)] += 1;
        if (piece.type == PieceType::King) {
            king_square_cache_[static_cast<std::size_t>(piece.owner)] = coordinate;
        }
    }
}

void GameState::transfer_pieces(int winner_id, int loser_id) {
    std::vector<std::string> erase_keys;
    for (auto& [key, piece] : pieces_) {
        if (piece.owner != loser_id) {
            continue;
        }
        if (piece.type == PieceType::King) {
            erase_keys.push_back(key);
            continue;
        }
        piece.owner = winner_id;
    }
    for (const std::string& key : erase_keys) {
        pieces_.erase(key);
    }
}

void GameState::remove_player(int player_id, std::string_view reason) {
    active_players_.erase(std::remove(active_players_.begin(), active_players_.end(), player_id), active_players_.end());
    log_.push_back("P" + std::to_string(player_id + 1) + " eliminated: " + std::string(reason));
}

void GameState::finish_if_one_player_left() {
    if (active_players_.size() == 1) {
        game_over_ = true;
        winner_ = active_players_.front();
        log_.push_back("Winner: P" + std::to_string(*winner_ + 1));
    }
}

void GameState::maybe_promote(const Coordinate& coordinate) {
    const std::string key = key_for(coordinate);
    auto iterator = pieces_.find(key);
    if (iterator == pieces_.end() || iterator->second.type != PieceType::Pawn) {
        return;
    }

    bool at_end = true;
    for (int axis = 1; axis < dimensions_; ++axis) {
        const int direction = iterator->second.pawn_direction[axis];
        if (direction == 0) {
            continue;
        }
        const int expected = direction > 0 ? board_size_ - 1 : 0;
        if (coordinate[axis] != expected) {
            at_end = false;
            break;
        }
    }
    if (!at_end) {
        return;
    }

    const bool has_queen = std::any_of(pieces_.begin(), pieces_.end(), [&](const auto& entry) {
        return entry.second.owner == iterator->second.owner && entry.second.type == PieceType::Queen;
    });
    iterator->second.type = has_queen ? PieceType::Knight : PieceType::Queen;
    iterator->second.pawn_direction.clear();
    log_.push_back("P" + std::to_string(iterator->second.owner + 1) + " promoted to " + piece_type_name(iterator->second.type));
}

void GameState::apply_move_unchecked(const Move& move, bool advance_turn_after, bool record_log) {
    const std::string from_key = key_for(move.from);
    const std::string to_key = key_for(move.to);
    Piece piece = pieces_.at(from_key);
    pieces_.erase(from_key);

    en_passant_target_.reset();
    en_passant_victim_square_.reset();
    en_passant_owner_.reset();

    if (move.is_en_passant && move.en_passant_capture_square.has_value()) {
        pieces_.erase(key_for(*move.en_passant_capture_square));
    }

    if (move.captured_piece.has_value() && move.captures_king) {
        const int loser = move.captured_piece->owner;
        pieces_.erase(to_key);
        piece.move_count += 1;
        pieces_[to_key] = piece;
        transfer_pieces(piece.owner, loser);
        remove_player(loser, "king captured");
        if (record_log) {
            log_.push_back("P" + std::to_string(piece.owner + 1) + " inherited P" + std::to_string(loser + 1) + " pieces");
        }
    } else {
        piece.move_count += 1;
        pieces_[to_key] = piece;
    }

    if (move.is_castle && move.rook_from.has_value() && move.rook_to.has_value()) {
        const std::string rook_from_key = key_for(*move.rook_from);
        const std::string rook_to_key = key_for(*move.rook_to);
        Piece rook = pieces_.at(rook_from_key);
        pieces_.erase(rook_from_key);
        rook.move_count += 1;
        pieces_[rook_to_key] = rook;
    }

    if (move.is_capture && !move.captures_king && record_log) {
        log_.push_back("P" + std::to_string(piece.owner + 1) + " captured " + piece_type_name(move.captured_piece->type));
    } else if (record_log) {
        log_.push_back("P" + std::to_string(piece.owner + 1) + " moved " + std::string(1, piece_type_symbol(piece.type)));
    }

    const Piece moved_piece = pieces_.at(to_key);
    if (moved_piece.type == PieceType::Pawn) {
        Coordinate delta(dimensions_, 0);
        for (int axis = 0; axis < dimensions_; ++axis) {
            delta[axis] = move.to[axis] - move.from[axis];
        }
        bool double_step = true;
        for (int axis = 0; axis < dimensions_; ++axis) {
            if (delta[axis] != moved_piece.pawn_direction[axis] * 2) {
                double_step = false;
                break;
            }
        }
        if (double_step) {
            Coordinate passover = move.from;
            for (int axis = 0; axis < dimensions_; ++axis) {
                passover[axis] += moved_piece.pawn_direction[axis];
            }
            en_passant_target_ = passover;
            en_passant_victim_square_ = move.to;
            en_passant_owner_ = moved_piece.owner;
        }
    }

    maybe_promote(move.to);
    rebuild_piece_cache();
    ++move_number_;
    finish_if_one_player_left();
    if (!game_over_ && advance_turn_after) {
        advance_turn();
    }
}

bool GameState::apply_move(const Coordinate& from, const Coordinate& to) {
    if (game_over_) {
        return false;
    }

    const std::optional<Piece> moving_piece = piece_at(from);
    if (!moving_piece.has_value() || moving_piece->owner != current_player_) {
        return false;
    }

    const std::vector<Move> legal = legal_moves_from(from);
    const auto iterator = std::find_if(legal.begin(), legal.end(), [&](const Move& move) {
        return move.to == to;
    });
    if (iterator == legal.end()) {
        return false;
    }

    apply_move_unchecked(*iterator, true, true);
    return true;
}

bool GameState::apply_resolved_move(const Move& move, bool record_log) {
    if (game_over_) {
        return false;
    }
    const std::optional<Piece> moving_piece = piece_at(move.from);
    if (!moving_piece.has_value() || moving_piece->owner != current_player_) {
        return false;
    }
    apply_move_unchecked(move, true, record_log);
    return true;
}

void GameState::eliminate_if_no_legal_moves() {
    if (game_over_) {
        return;
    }

    while (!game_over_) {
        if (has_any_legal_move_for_player(current_player_)) {
            return;
        }

        const int eliminated = current_player_;
        remove_player(eliminated, "no legal moves");
        for (auto iterator = pieces_.begin(); iterator != pieces_.end();) {
            if (iterator->second.owner == eliminated) {
                iterator = pieces_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        rebuild_piece_cache();
        finish_if_one_player_left();
        if (!game_over_) {
            advance_turn();
        }
    }
}

void GameState::advance_turn() {
    if (active_players_.empty()) {
        game_over_ = true;
        winner_.reset();
        return;
    }

    auto iterator = std::find(active_players_.begin(), active_players_.end(), current_player_);
    if (iterator == active_players_.end()) {
        current_player_ = active_players_.front();
    } else {
        const std::size_t index = static_cast<std::size_t>(std::distance(active_players_.begin(), iterator));
        current_player_ = active_players_[(index + 1) % active_players_.size()];
    }
    eliminate_if_no_legal_moves();
}

SimulationRunner::SimulationRunner(std::uint64_t seed)
    : rng_(seed == 0 ? std::random_device{}() : seed) {}

Move SimulationRunner::choose_move(const GameState& state, int player_id) {
    if (state.dimensions() >= 7) {
        return choose_move_fast_high_d(state, player_id);
    }
    const std::vector<Move> moves = state.legal_moves_for_player(player_id);
    return choose_move_from_legal(moves);
}

Move SimulationRunner::choose_move_from_legal(const std::vector<Move>& moves) {
    if (moves.empty()) {
        return {};
    }
    Move best = moves.front();
    double best_score = score_move(best);
    for (const Move& move : moves) {
        const double score = score_move(move);
        if (score > best_score) {
            best = move;
            best_score = score;
        }
    }
    return best;
}

Move SimulationRunner::choose_move_fast_high_d(const GameState& state, int player_id, int* candidate_count) {
    const std::vector<Move> candidates = state.candidate_moves_for_player(
        player_id,
        state.dimensions() >= 8 ? 3 : 4,
        state.dimensions() >= 8 ? 32 : 48);
    if (candidate_count) {
        *candidate_count = static_cast<int>(candidates.size());
    }
    if (candidates.empty()) {
        return {};
    }

    Move best {};
    double best_score = -1.0e18;
    int legal_hits = 0;
    const int legal_target = state.dimensions() >= 8 ? 6 : 10;
    const int validation_limit = state.dimensions() >= 8 ? 24 : 40;
    const std::size_t max_checks = (std::min)(candidates.size(), static_cast<std::size_t>(validation_limit));
    for (std::size_t index = 0; index < max_checks; ++index) {
        const Move& move = candidates[index];
        if (!state.move_keeps_king_safe(move, player_id)) {
            continue;
        }
        const double score = score_move(move);
        if (!best.from.size() || score > best_score) {
            best = move;
            best_score = score;
        }
        ++legal_hits;
        if (move.captures_king || legal_hits >= legal_target) {
            break;
        }
    }
    if (!best.from.empty()) {
        return best;
    }
    return choose_move_from_legal(state.legal_moves_for_player(player_id));
}

double SimulationRunner::score_move(const Move& move) {
    double score = 0.0;
    if (move.captures_king) {
        score += 10000.0;
    }
    if (move.promotion) {
        score += 100.0;
    }
    if (move.is_capture && move.captured_piece.has_value()) {
        switch (move.captured_piece->type) {
            case PieceType::King: score += 1000.0; break;
            case PieceType::Queen: score += 90.0; break;
            case PieceType::Rook: score += 50.0; break;
            case PieceType::Bishop: score += 30.0; break;
            case PieceType::Knight: score += 30.0; break;
            case PieceType::Pawn: score += 10.0; break;
        }
    }

    for (int value : move.to) {
        score -= std::abs(value - 3.5) * 0.1;
    }

    std::uniform_real_distribution<double> noise(0.0, 0.2);
    score += noise(rng_);
    return score;
}

SimulationSummary SimulationRunner::run_games(int dimensions, int games, int max_moves) {
    SimulationSummary summary;
    summary.dimensions = dimensions;
    summary.games = games;
    summary.max_moves = max_moves;

    for (int game_index = 0; game_index < games; ++game_index) {
        GameState state(dimensions);
        state.eliminate_if_no_legal_moves();

        int applied_moves = 0;
        while (!state.game_over() && applied_moves < max_moves) {
            const int player_id = state.current_player();
            Move chosen {};
            if (dimensions >= 7) {
                if (!state.has_any_legal_move_for_player(player_id)) {
                    state.eliminate_if_no_legal_moves();
                    continue;
                }
                chosen = choose_move_fast_high_d(state, player_id);
            } else {
                const std::vector<Move> legal = state.legal_moves_for_player(player_id);
                if (legal.empty()) {
                    state.eliminate_if_no_legal_moves();
                    continue;
                }
                chosen = choose_move_from_legal(legal);
            }
            if (!state.apply_resolved_move(chosen, false)) {
                break;
            }
            ++applied_moves;
        }

        summary.total_moves += applied_moves;
        if (state.winner().has_value()) {
            summary.wins_by_player[*state.winner()] += 1;
        }
    }

    return summary;
}

TurnAdvice SimulationRunner::suggest_turn(const GameState& state, int player_id) {
    TurnAdvice advice;
    advice.player_id = player_id;
    advice.in_check = state.is_in_check(player_id);
    if (advice.in_check) {
        advice.king_threats = state.king_threats(player_id);
    }
    if (state.dimensions() >= 7 && !advice.in_check) {
        int candidate_count = 0;
        advice.move = choose_move_fast_high_d(state, player_id, &candidate_count);
        advice.legal_move_count = candidate_count;
        advice.has_move = !advice.move.from.empty();
        if (advice.has_move) {
            return advice;
        }
    }
    const std::vector<Move> legal = state.legal_moves_for_player(player_id);
    advice.legal_move_count = static_cast<int>(legal.size());
    if (legal.empty()) {
        return advice;
    }
    advice.has_move = true;
    advice.move = choose_move_from_legal(legal);
    return advice;
}

PersonalityProfile SimulationRunner::make_personality(int player_id) {
    std::uniform_real_distribution<double> dist(0.2, 0.95);
    PersonalityProfile profile;
    profile.label = "P" + std::to_string(player_id + 1);
    profile.honesty = dist(rng_);
    profile.cooperation = dist(rng_);
    profile.prosocial = dist(rng_);
    profile.trusting = dist(rng_);
    profile.communicative = dist(rng_);
    profile.aggression = dist(rng_);
    profile.foresight = dist(rng_);
    profile.defensiveness = dist(rng_);
    profile.patience = dist(rng_);
    profile.regicide = dist(rng_);
    profile.vendetta = dist(rng_);
    profile.greed = dist(rng_);
    profile.chaos = dist(rng_);
    profile.attention = dist(rng_);
    profile.alliance_comfort = dist(rng_);
    return profile;
}

GameLog SimulationRunner::run_game_log(int dimensions, int max_moves, bool until_winner, int checkpoint_interval, int replay_segment_span) {
    GameLog log;
    log.generated_at = iso_timestamp_now();
    log.dimensions = dimensions;
    log.players = 1 << (dimensions - 1);
    log.move_cap = until_winner ? "until_winner" : std::to_string(max_moves);

    for (int player_id = 0; player_id < log.players; ++player_id) {
        log.player_roster.push_back(make_personality(player_id));
    }

    GameState state(dimensions);
    state.eliminate_if_no_legal_moves();
    ReplaySegment current_segment;
    current_segment.start_move = 0;
    current_segment.start_state_json = state.serialize_state_json();

    std::map<int, int> previous_counts;
    std::vector<std::vector<double>> trust(log.players, std::vector<double>(log.players, 0.0));
    std::set<std::pair<int, int>> truces;
    for (int player_id = 0; player_id < log.players; ++player_id) {
        previous_counts[player_id] = state.piece_count(player_id);
        for (int other = 0; other < log.players; ++other) {
            if (player_id == other) {
                continue;
            }
            trust[player_id][other] = 0.35 + ((log.player_roster[player_id].alliance_comfort + log.player_roster[player_id].cooperation) / 4.0);
        }
    }

    int applied_moves = 0;
    int captures = 0;
    int next_chat_index = 1;
    while (!state.game_over() && (until_winner || applied_moves < max_moves)) {
        const std::size_t replay_event_count_before = log.replay_events.size();
        const int player_id = state.current_player();
        Move chosen {};
        if (dimensions >= 7) {
            if (!state.has_any_legal_move_for_player(player_id)) {
                state.eliminate_if_no_legal_moves();
                continue;
            }
            chosen = choose_move_fast_high_d(state, player_id);
        } else {
            const std::vector<Move> legal = state.legal_moves_for_player(player_id);
            if (legal.empty()) {
                state.eliminate_if_no_legal_moves();
                continue;
            }
            chosen = choose_move_from_legal(legal);
        }
        int captured_owner = -1;
        if (chosen.captured_piece.has_value()) {
            captured_owner = chosen.captured_piece->owner;
        }

        if (chosen.from.empty() || chosen.to.empty() || !state.apply_resolved_move(chosen, true)) {
            break;
        }

        ++applied_moves;
        if (chosen.is_capture) {
            ++captures;
        }

        if (captured_owner >= 0 && captured_owner < log.players && captured_owner != player_id) {
            const auto edge = std::minmax(player_id, captured_owner);
            const bool was_truce = truces.contains(edge);
            log.replay_events.push_back(ReplayEvent{
                .move = applied_moves,
                .type = was_truce ? "betrayal" : "capture",
                .label = was_truce
                    ? "P" + std::to_string(player_id + 1) + " betrayed P" + std::to_string(captured_owner + 1)
                    : "P" + std::to_string(player_id + 1) + " captured from P" + std::to_string(captured_owner + 1),
                .actor = player_id,
                .target = captured_owner,
            });
            if (was_truce) {
                truces.erase(edge);
                for (int observer = 0; observer < log.players; ++observer) {
                    if (observer == player_id || observer == captured_owner) {
                        continue;
                    }
                    trust[observer][player_id] = std::max(0.0, trust[observer][player_id] - 0.18);
                }
                log.chat_log.push_back(ChatMessage{
                    .index = next_chat_index++,
                    .turn = applied_moves,
                    .sender = captured_owner,
                    .recipients = {},
                    .is_public = true,
                    .mode = "headless",
                    .content = "P" + std::to_string(player_id + 1) + " broke truce and struck P" + std::to_string(captured_owner + 1) + ".",
                    .target = player_id,
                });
                current_segment.chat_log.push_back(log.chat_log.back());
            }
            trust[captured_owner][player_id] = std::max(0.0, trust[captured_owner][player_id] - 0.25);
            trust[player_id][captured_owner] = std::max(0.0, trust[player_id][captured_owner] - (was_truce ? 0.12 : 0.04));
        }

        if (applied_moves % 3 == 0 && !state.game_over()) {
            int leader = -1;
            int leader_count = -1;
            for (int pid : state.active_players()) {
                const int count = state.piece_count(pid);
                if (count > leader_count) {
                    leader = pid;
                    leader_count = count;
                }
            }

            int ally = -1;
            double ally_score = -1.0;
            for (int pid : state.active_players()) {
                if (pid == player_id) {
                    continue;
                }
                if (trust[player_id][pid] > ally_score) {
                    ally = pid;
                    ally_score = trust[player_id][pid];
                }
            }

            if (ally >= 0 && ally_score > 0.58) {
                truces.insert(std::minmax(player_id, ally));
                trust[player_id][ally] = std::min(1.0, trust[player_id][ally] + 0.03);
                trust[ally][player_id] = std::min(1.0, trust[ally][player_id] + 0.02);
                log.replay_events.push_back(ReplayEvent{
                    .move = applied_moves,
                    .type = "truce",
                    .label = "P" + std::to_string(player_id + 1) + " and P" + std::to_string(ally + 1) + " aligned",
                    .actor = player_id,
                    .target = ally,
                });
                log.chat_log.push_back(ChatMessage{
                    .index = next_chat_index++,
                    .turn = applied_moves,
                    .sender = player_id,
                    .recipients = {ally},
                    .is_public = false,
                    .mode = "headless",
                    .content = "P" + std::to_string(ally + 1) + ", hold the line with me for a while.",
                    .target = leader >= 0 && leader != player_id ? std::optional<int>(leader) : std::nullopt,
                });
                current_segment.chat_log.push_back(log.chat_log.back());
            } else if (leader >= 0 && leader != player_id) {
                log.replay_events.push_back(ReplayEvent{
                    .move = applied_moves,
                    .type = "dogpile_call",
                    .label = "P" + std::to_string(player_id + 1) + " called attention to P" + std::to_string(leader + 1),
                    .actor = player_id,
                    .target = leader,
                });
                log.chat_log.push_back(ChatMessage{
                    .index = next_chat_index++,
                    .turn = applied_moves,
                    .sender = player_id,
                    .recipients = {},
                    .is_public = true,
                    .mode = "headless",
                    .content = "P" + std::to_string(leader + 1) + " is getting too strong. Close the ring.",
                    .target = leader,
                });
                current_segment.chat_log.push_back(log.chat_log.back());
                for (int pid : state.active_players()) {
                    if (pid != player_id && pid != leader) {
                        trust[pid][leader] = std::max(0.0, trust[pid][leader] - 0.02);
                    }
                }
            }
        }

        if (checkpoint_interval > 0 && (applied_moves % checkpoint_interval == 0 || state.game_over())) {
            ResearchCheckpoint checkpoint;
            checkpoint.move = applied_moves;
            checkpoint.players_remaining = static_cast<int>(state.active_players().size());
            int leader = -1;
            int leader_count = -1;
            for (int pid = 0; pid < log.players; ++pid) {
                const int count = state.piece_count(pid);
                checkpoint.piece_counts[pid] = count;
                if (count > leader_count) {
                    leader = pid;
                    leader_count = count;
                }
            }
            std::set<int> visited;
            for (int pid : state.active_players()) {
                if (visited.contains(pid)) {
                    continue;
                }
                std::vector<int> component;
                std::function<void(int)> dfs = [&](int node) {
                    if (visited.contains(node)) {
                        return;
                    }
                    visited.insert(node);
                    component.push_back(node);
                    for (int other : state.active_players()) {
                        if (other == node) {
                            continue;
                        }
                        if (truces.contains(std::minmax(node, other))) {
                            dfs(other);
                        }
                    }
                };
                dfs(pid);
                if (component.size() >= 2) {
                    std::sort(component.begin(), component.end());
                    checkpoint.alliances.push_back(std::move(component));
                }
            }
            if (leader >= 0) {
                checkpoint.leader = leader;
            }
            checkpoint.state_json = state.serialize_state_json();
            log.research_checkpoints.push_back(std::move(checkpoint));
        }

        if (!state.log().empty()) {
            const std::string& latest = state.log().back();
            if (current_segment.move_log.empty() || current_segment.move_log.back() != latest) {
                current_segment.move_log.push_back(latest);
            }
        }
        if (log.replay_events.size() > replay_event_count_before) {
            for (std::size_t event_index = replay_event_count_before; event_index < log.replay_events.size(); ++event_index) {
                current_segment.replay_events.push_back(log.replay_events[event_index]);
            }
        }
        current_segment.end_move = applied_moves;
        const int segment_span = std::max(1, replay_segment_span);
        if (applied_moves % segment_span == 0) {
            log.replay_segments.push_back(current_segment);
            current_segment = ReplaySegment{};
            current_segment.start_move = applied_moves;
            current_segment.start_state_json = state.serialize_state_json();
        }
    }

    log.moves_played = applied_moves;
    log.players_remaining = static_cast<int>(state.active_players().size());
    log.captures = captures;
    log.snapshot = snapshot_label(applied_moves);
    log.move_log = state.log();
    log.winner = state.winner();
    if (!current_segment.start_state_json.empty() &&
        (current_segment.end_move > current_segment.start_move ||
         !current_segment.move_log.empty() ||
         !current_segment.chat_log.empty() ||
         !current_segment.replay_events.empty())) {
        log.replay_segments.push_back(current_segment);
    }
    if (log.winner.has_value()) {
        log.replay_events.push_back(ReplayEvent{
            .move = applied_moves,
            .type = "winner",
            .label = "P" + std::to_string(*log.winner + 1) + " won the game",
            .actor = log.winner,
            .target = std::nullopt,
        });
    }
    return log;
}

EvoBatchResult SimulationRunner::run_evo_batch(const EvoConfig& config) {
    EvoBatchResult batch;
    batch.config = config;

    std::map<int, int> total_piece_pressure;
    std::map<int, double> fitness_by_player;
    const int total_players = 1 << (config.dimensions - 1);

    for (int generation = 0; generation < config.generations; ++generation) {
        for (int game_index = 0; game_index < config.games_per_generation; ++game_index) {
            GameLog log = run_game_log(config.dimensions,
                                       config.max_moves,
                                       config.until_winner,
                                       config.checkpoint_interval,
                                       config.replay_segment_span);
            batch.total_games += 1;
            batch.total_moves += log.moves_played;
            if (log.winner.has_value()) {
                batch.wins_by_player[*log.winner] += 1;
                fitness_by_player[*log.winner] += 10.0;
            }
            for (const auto& checkpoint : log.research_checkpoints) {
                for (const auto& [player_id, pieces] : checkpoint.piece_counts) {
                    total_piece_pressure[player_id] += pieces;
                    fitness_by_player[player_id] += static_cast<double>(pieces) / 1000.0;
                }
            }
            batch.run_logs.push_back(std::move(log));
        }
    }

    std::vector<std::pair<int, double>> ranked;
    ranked.reserve(total_players);
    for (int player_id = 0; player_id < total_players; ++player_id) {
        ranked.push_back({player_id, fitness_by_player[player_id]});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second == rhs.second) {
            return lhs.first < rhs.first;
        }
        return lhs.second > rhs.second;
    });

    for (std::size_t index = 0; index < ranked.size(); ++index) {
        ChampionRecord champion;
        champion.rank = static_cast<int>(index) + 1;
        champion.label = "P" + std::to_string(ranked[index].first + 1);
        champion.fitness = ranked[index].second;
        champion.avg_move_ms = batch.total_games == 0 ? 0.0 : static_cast<double>(batch.total_moves) / std::max(1, batch.total_games);
        champion.avg_power = total_piece_pressure[ranked[index].first] == 0 ? 0.0 : static_cast<double>(total_piece_pressure[ranked[index].first]) / std::max(1, batch.total_games);
        champion.speed_grade = champion.avg_move_ms < 100.0 ? "A" : (champion.avg_move_ms < 250.0 ? "B" : "C");
        champion.power_grade = champion.avg_power > 500.0 ? "A" : (champion.avg_power > 200.0 ? "B" : "C");
        const PersonalityProfile profile = make_personality(ranked[index].first);
        champion.aggression = profile.aggression;
        champion.foresight = profile.foresight;
        champion.defensiveness = profile.defensiveness;
        champion.patience = profile.patience;
        champion.regicide = profile.regicide;
        champion.vendetta = profile.vendetta;
        champion.greed = profile.greed;
        champion.chaos = profile.chaos;
        champion.attention = profile.attention;
        champion.alliance_comfort = profile.alliance_comfort;
        batch.champions.push_back(std::move(champion));
    }

    return batch;
}

std::string serialize_game_log_json(const GameLog& log) {
    std::ostringstream output;
    output << "{"
           << "\"title\":\"" << json_escape(log.title) << "\","
           << "\"generated_at\":\"" << json_escape(log.generated_at) << "\","
           << "\"game\":\"" << json_escape(log.game) << "\","
           << "\"mode\":\"" << json_escape(log.mode) << "\","
           << "\"dimensions\":" << log.dimensions << ","
           << "\"players\":" << log.players << ","
           << "\"move_cap\":\"" << json_escape(log.move_cap) << "\","
           << "\"snapshot\":\"" << json_escape(log.snapshot) << "\","
           << "\"winner\":";
    if (log.winner.has_value()) {
        output << *log.winner;
    } else {
        output << "null";
    }
    output << ",\"moves_played\":" << log.moves_played
           << ",\"players_remaining\":" << log.players_remaining
           << ",\"captures\":" << log.captures
           << ",\"player_roster\":[";
    for (std::size_t index = 0; index < log.player_roster.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << personality_json(log.player_roster[index]);
    }
    output << "],\"research_checkpoints\":[";
    for (std::size_t index = 0; index < log.research_checkpoints.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << checkpoint_json(log.research_checkpoints[index]);
    }
    output << "],\"replay_events\":[";
    for (std::size_t index = 0; index < log.replay_events.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << replay_event_json(log.replay_events[index]);
    }
    output << "],\"replay_segments\":[";
    for (std::size_t index = 0; index < log.replay_segments.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << replay_segment_json(log.replay_segments[index]);
    }
    output << "],\"move_log\":[";
    for (std::size_t index = 0; index < log.move_log.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << "\"" << json_escape(log.move_log[index]) << "\"";
    }
    output << "],\"chat_log\":[";
    for (std::size_t index = 0; index < log.chat_log.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << chat_message_json(log.chat_log[index]);
    }
    output << "]}";
    return output.str();
}

std::string serialize_game_log_text(const GameLog& log) {
    std::ostringstream output;
    output << log.title << "\n";
    output << "Generated: " << log.generated_at << "\n";
    output << "Game: " << log.game << " | Mode: " << log.mode << "\n";
    output << "Dimensions: " << log.dimensions << " | Players: " << log.players << "\n";
    output << "Move cap: " << log.move_cap << " | Moves played: " << log.moves_played << "\n";
    output << "Winner: ";
    if (log.winner.has_value()) {
        output << "P" << (*log.winner + 1);
    } else {
        output << "None";
    }
    output << "\nCaptures: " << log.captures << "\n\n";
    output << "Communications:\n";
    if (log.chat_log.empty()) {
        output << "  (none)\n";
    } else {
        for (const auto& message : log.chat_log) {
            output << "  " << std::setw(4) << message.index << ". TURN " << message.turn
                   << " P" << (message.sender + 1) << (message.is_public ? " -> ALL" : " -> ");
            if (!message.is_public) {
                for (std::size_t index = 0; index < message.recipients.size(); ++index) {
                    if (index != 0) {
                        output << ",";
                    }
                    output << "P" << (message.recipients[index] + 1);
                }
            }
            output << ": \"" << message.content << "\"\n";
        }
    }
    output << "\nReplay Events:\n";
    if (log.replay_events.empty()) {
        output << "  (none)\n";
    } else {
        for (const auto& event : log.replay_events) {
            output << "  TURN " << event.move << " [" << event.type << "] " << event.label << "\n";
        }
    }
    output << "\nReplay Segments:\n";
    if (log.replay_segments.empty()) {
        output << "  (none)\n";
    } else {
        for (const auto& segment : log.replay_segments) {
            output << "  Moves " << segment.start_move << "-" << segment.end_move
                   << " | moves=" << segment.move_log.size()
                   << " | chats=" << segment.chat_log.size()
                   << " | events=" << segment.replay_events.size() << "\n";
        }
    }
    output << "\nMove Log:\n";
    for (const auto& move : log.move_log) {
        output << "  " << move << "\n";
    }
    return output.str();
}

std::string serialize_turn_advice_json(const TurnAdvice& advice) {
    return turn_advice_json(advice);
}

std::string serialize_champion_export_json(const std::vector<ChampionRecord>& champions,
                                           int dimensions,
                                           std::string_view mode,
                                           std::string_view generated_at) {
    std::ostringstream output;
    output << "{"
           << "\"source\":\"eightd-cpp-headless\","
           << "\"date\":\"" << json_escape(generated_at) << "\","
           << "\"mode\":\"" << json_escape(mode) << "\","
           << "\"dimensions\":" << dimensions << ","
           << "\"champions\":[";
    for (std::size_t index = 0; index < champions.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << champion_json(champions[index]);
    }
    output << "]}";
    return output.str();
}

std::string serialize_evo_batch_json(const EvoBatchResult& batch) {
    std::ostringstream output;
    output << "{"
           << "\"config\":{"
           << "\"dimensions\":" << batch.config.dimensions << ","
           << "\"generations\":" << batch.config.generations << ","
           << "\"gamesPerGeneration\":" << batch.config.games_per_generation << ","
           << "\"maxMoves\":" << batch.config.max_moves << ","
           << "\"untilWinner\":" << json_bool(batch.config.until_winner) << ","
           << "\"checkpointInterval\":" << batch.config.checkpoint_interval
           << "},"
           << "\"totalGames\":" << batch.total_games << ","
           << "\"totalMoves\":" << batch.total_moves << ","
           << "\"winsByPlayer\":" << json_number_map(batch.wins_by_player) << ","
           << "\"champions\":[";
    for (std::size_t index = 0; index < batch.champions.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << champion_json(batch.champions[index]);
    }
    output << "],\"runLogs\":[";
    for (std::size_t index = 0; index < batch.run_logs.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << serialize_game_log_json(batch.run_logs[index]);
    }
    output << "]}";
    return output.str();
}

}  // namespace eightd
