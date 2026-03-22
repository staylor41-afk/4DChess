#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eightd {

using Coordinate = std::vector<int>;

enum class PieceType {
    King,
    Queen,
    Rook,
    Bishop,
    Knight,
    Pawn,
};

struct Piece {
    PieceType type {};
    int owner = -1;
    Coordinate pawn_direction {};
    int original_owner = -1;
    int move_count = 0;
};

struct Move {
    Coordinate from {};
    Coordinate to {};
    bool is_capture = false;
    std::optional<Piece> captured_piece {};
    bool captures_king = false;
    bool promotion = false;
    std::optional<PieceType> promotion_type {};
    bool is_castle = false;
    std::optional<Coordinate> rook_from {};
    std::optional<Coordinate> rook_to {};
    bool is_en_passant = false;
    std::optional<Coordinate> en_passant_capture_square {};
};

struct SliceRequest {
    std::vector<int> view_axes {};
    std::map<int, int> fixed_axes {};
};

struct SimulationSummary {
    int dimensions = 0;
    int games = 0;
    int max_moves = 0;
    int total_moves = 0;
    std::map<int, int> wins_by_player {};
};

struct PersonalityProfile {
    std::string label {};
    double honesty = 0.5;
    double cooperation = 0.5;
    double prosocial = 0.5;
    double trusting = 0.5;
    double communicative = 0.5;
    double aggression = 0.5;
    double foresight = 0.5;
    double defensiveness = 0.5;
    double patience = 0.5;
    double regicide = 0.5;
    double vendetta = 0.5;
    double greed = 0.5;
    double chaos = 0.5;
    double attention = 0.5;
    double alliance_comfort = 0.5;
};

struct ResearchCheckpoint {
    int move = 0;
    int players_remaining = 0;
    std::optional<int> leader {};
    std::map<int, int> piece_counts {};
    std::vector<std::vector<int>> alliances {};
    std::string state_json {};
};

struct ChatMessage {
    int index = 0;
    int turn = 0;
    int sender = 0;
    std::vector<int> recipients {};
    bool is_public = false;
    std::string mode = "headless";
    std::string book {};
    std::string content {};
    std::optional<int> target {};
};

struct ReplayEvent {
    int move = 0;
    std::string type {};
    std::string label {};
    std::optional<int> actor {};
    std::optional<int> target {};
};

struct ReplaySegment {
    int start_move = 0;
    int end_move = 0;
    std::string start_state_json {};
    std::vector<std::string> move_log {};
    std::vector<ChatMessage> chat_log {};
    std::vector<ReplayEvent> replay_events {};
};

struct TurnAdvice {
    int player_id = -1;
    bool has_move = false;
    Move move {};
    bool in_check = false;
    int legal_move_count = 0;
    std::vector<Coordinate> king_threats {};
};

struct GameLog {
    std::string title = "8D Chess C++ Headless Run";
    std::string generated_at {};
    std::string game = "chess";
    std::string mode = "headless-evo";
    int dimensions = 0;
    int players = 0;
    std::string move_cap = "capped";
    std::string snapshot = "Opening";
    std::optional<int> winner {};
    int moves_played = 0;
    int players_remaining = 0;
    int captures = 0;
    std::vector<PersonalityProfile> player_roster {};
    std::vector<ResearchCheckpoint> research_checkpoints {};
    std::vector<ReplayEvent> replay_events {};
    std::vector<ReplaySegment> replay_segments {};
    std::vector<std::string> move_log {};
    std::vector<ChatMessage> chat_log {};
};

struct ChampionRecord {
    int rank = 0;
    std::string label {};
    double aggression = 0.5;
    double foresight = 0.5;
    double defensiveness = 0.5;
    double patience = 0.5;
    double regicide = 0.5;
    double vendetta = 0.5;
    double greed = 0.5;
    double chaos = 0.5;
    double attention = 0.5;
    double alliance_comfort = 0.5;
    double fitness = 0.0;
    double avg_move_ms = 0.0;
    double avg_power = 0.0;
    std::string speed_grade = "C";
    std::string power_grade = "C";
};

struct EvoConfig {
    int dimensions = 2;
    int generations = 1;
    int games_per_generation = 1;
    int max_moves = 200;
    bool until_winner = false;
    int checkpoint_interval = 50;
    int replay_segment_span = 1000;
    std::uint64_t seed = 0;
};

struct EvoBatchResult {
    EvoConfig config {};
    int total_games = 0;
    int total_moves = 0;
    std::map<int, int> wins_by_player {};
    std::vector<ChampionRecord> champions {};
    std::vector<GameLog> run_logs {};
};

class GameState {
public:
    GameState() = default;
    explicit GameState(int dimensions);

    void reset(int dimensions);
    void clear_for_testing();
    void set_current_player(int player_id);
    void set_piece(const Coordinate& coordinate, const Piece& piece);

    [[nodiscard]] int dimensions() const noexcept { return dimensions_; }
    [[nodiscard]] int board_size() const noexcept { return board_size_; }
    [[nodiscard]] int player_count() const noexcept { return player_count_; }
    [[nodiscard]] int current_player() const noexcept { return current_player_; }
    [[nodiscard]] int move_number() const noexcept { return move_number_; }
    [[nodiscard]] bool game_over() const noexcept { return game_over_; }
    [[nodiscard]] std::optional<int> winner() const noexcept { return winner_; }
    [[nodiscard]] const std::vector<int>& active_players() const noexcept { return active_players_; }
    [[nodiscard]] const std::vector<std::string>& log() const noexcept { return log_; }
    [[nodiscard]] const std::unordered_map<std::string, Piece>& pieces() const noexcept { return pieces_; }
    [[nodiscard]] const std::optional<Coordinate>& en_passant_target() const noexcept { return en_passant_target_; }

    [[nodiscard]] Coordinate player_home(int player_id) const;
    [[nodiscard]] std::vector<Move> legal_moves_from(const Coordinate& from) const;
    [[nodiscard]] std::vector<Move> legal_moves_for_player(int player_id) const;
    [[nodiscard]] std::vector<Move> candidate_moves_for_player(int player_id,
                                                               std::size_t per_piece_limit = 6,
                                                               std::size_t total_limit = 96) const;
    [[nodiscard]] bool has_any_legal_move_for_player(int player_id) const;
    [[nodiscard]] bool move_keeps_king_safe(const Move& move, int player_id) const;
    [[nodiscard]] bool has_king(int player_id) const;
    [[nodiscard]] int piece_count(int player_id) const;
    [[nodiscard]] std::vector<Coordinate> king_threats(int player_id) const;
    [[nodiscard]] bool is_in_check(int player_id) const;
    [[nodiscard]] std::vector<std::pair<Coordinate, Piece>> slice_view(const SliceRequest& request) const;
    [[nodiscard]] std::string serialize_state_json(bool include_log = true, bool include_checks = true) const;
    [[nodiscard]] std::string serialize_slice_json(const SliceRequest& request) const;
    [[nodiscard]] std::string serialize_moves_json(const std::vector<Move>& moves) const;

    bool apply_move(const Coordinate& from, const Coordinate& to);
    bool apply_resolved_move(const Move& move, bool record_log = true);
    void eliminate_if_no_legal_moves();

private:
    int dimensions_ = 0;
    int board_size_ = 8;
    int player_count_ = 0;
    int current_player_ = 0;
    int move_number_ = 0;
    bool game_over_ = false;
    std::optional<int> winner_ {};
    std::unordered_map<std::string, Piece> pieces_ {};
    std::vector<int> active_players_ {};
    std::vector<std::string> log_ {};
    std::optional<Coordinate> en_passant_target_ {};
    std::optional<Coordinate> en_passant_victim_square_ {};
    std::optional<int> en_passant_owner_ {};
    std::vector<std::vector<std::pair<Coordinate, Piece>>> player_piece_cache_ {};
    std::vector<int> piece_count_cache_ {};
    std::vector<std::optional<Coordinate>> king_square_cache_ {};

    [[nodiscard]] std::string key_for(const Coordinate& coordinate) const;
    [[nodiscard]] std::optional<Piece> piece_at(const Coordinate& coordinate) const;
    [[nodiscard]] bool in_bounds(const Coordinate& coordinate) const;
    [[nodiscard]] std::vector<Coordinate> piece_coordinates_for_player(int player_id) const;
    [[nodiscard]] std::vector<Move> pseudo_legal_moves_from(const Coordinate& from) const;
    [[nodiscard]] bool square_attacked_by(int attacker_id, const Coordinate& target) const;
    [[nodiscard]] bool piece_attacks_square(const Coordinate& from, const Piece& piece, const Coordinate& target) const;
    [[nodiscard]] bool path_clear(const Coordinate& from, const Coordinate& to, const Coordinate& step) const;

    void place_initial_pieces();
    void place_back_rank(int player_id, const Coordinate& home);
    void place_pawns(int player_id, const Coordinate& home);
    void transfer_pieces(int winner_id, int loser_id);
    void remove_player(int player_id, std::string_view reason);
    void advance_turn();
    void maybe_promote(const Coordinate& coordinate);
    void finish_if_one_player_left();
    void apply_move_unchecked(const Move& move, bool advance_turn, bool record_log);
    void rebuild_piece_cache();
};

class SimulationRunner {
public:
    explicit SimulationRunner(std::uint64_t seed = 0);

    SimulationSummary run_games(int dimensions, int games, int max_moves);
    TurnAdvice suggest_turn(const GameState& state, int player_id);
    GameLog run_game_log(int dimensions,
                         int max_moves,
                         bool until_winner = false,
                         int checkpoint_interval = 50,
                         int replay_segment_span = 1000);
    EvoBatchResult run_evo_batch(const EvoConfig& config);

private:
    std::mt19937_64 rng_;

    Move choose_move(const GameState& state, int player_id);
    Move choose_move_from_legal(const std::vector<Move>& moves);
    Move choose_move_fast_high_d(const GameState& state, int player_id, int* candidate_count = nullptr);
    double score_move(const Move& move);
    PersonalityProfile make_personality(int player_id);
};

std::string piece_type_name(PieceType type);
char piece_type_symbol(PieceType type);
Coordinate parse_coordinate(std::string_view text);
SliceRequest parse_slice_request(std::string_view view_text, std::string_view fixed_text);
std::string serialize_game_log_json(const GameLog& log);
std::string serialize_game_log_text(const GameLog& log);
std::string serialize_turn_advice_json(const TurnAdvice& advice);
std::string serialize_champion_export_json(const std::vector<ChampionRecord>& champions,
                                           int dimensions,
                                           std::string_view mode,
                                           std::string_view generated_at);
std::string serialize_evo_batch_json(const EvoBatchResult& batch);

}  // namespace eightd
