#include "eightd/engine.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void assert_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_initial_setup() {
    eightd::GameState game(2);
    assert_true(game.player_count() == 2, "2D should have 2 players");
    assert_true(game.piece_count(0) == 16, "player 1 should start with 16 pieces");
    assert_true(game.piece_count(1) == 16, "player 2 should start with 16 pieces");
}

void test_opening_pawn_move() {
    eightd::GameState game(2);
    const auto moves = game.legal_moves_from({0, 1});
    bool one_step = false;
    bool two_step = false;
    for (const auto& move : moves) {
        if (move.to == eightd::Coordinate{0, 2}) {
            one_step = true;
        }
        if (move.to == eightd::Coordinate{0, 3}) {
            two_step = true;
        }
    }
    assert_true(one_step, "pawn should move one square");
    assert_true(two_step, "pawn should move two squares from start");
}

void test_turn_advances() {
    eightd::GameState game(2);
    assert_true(game.apply_move({0, 1}, {0, 3}), "opening move should succeed");
    assert_true(game.current_player() == 1, "turn should advance to second player");
}

void test_promotion() {
    eightd::GameState game(2);
    game.clear_for_testing();
    game.set_current_player(0);
    game.set_piece({4, 0}, eightd::Piece{.type = eightd::PieceType::King, .owner = 0, .pawn_direction = {}, .original_owner = 0});
    game.set_piece({4, 7}, eightd::Piece{.type = eightd::PieceType::King, .owner = 1, .pawn_direction = {}, .original_owner = 1});
    game.set_piece({0, 6}, eightd::Piece{.type = eightd::PieceType::Pawn, .owner = 0, .pawn_direction = {0, 1}, .original_owner = 0});
    assert_true(game.apply_move({0, 6}, {0, 7}), "promotion move should be legal");
    const auto promoted = game.pieces().at("0,7");
    assert_true(promoted.type == eightd::PieceType::Queen, "first promotion should become queen");
}

void test_king_capture() {
    eightd::GameState game(2);
    game.clear_for_testing();
    game.set_current_player(0);
    game.set_piece({4, 0}, eightd::Piece{.type = eightd::PieceType::King, .owner = 0, .pawn_direction = {}, .original_owner = 0});
    game.set_piece({4, 7}, eightd::Piece{.type = eightd::PieceType::King, .owner = 1, .pawn_direction = {}, .original_owner = 1});
    game.set_piece({4, 6}, eightd::Piece{.type = eightd::PieceType::Queen, .owner = 0, .pawn_direction = {}, .original_owner = 0});
    game.set_piece({0, 7}, eightd::Piece{.type = eightd::PieceType::Rook, .owner = 1, .pawn_direction = {}, .original_owner = 1});
    assert_true(game.apply_move({4, 6}, {4, 7}), "king capture should succeed");
    assert_true(game.game_over(), "game should end after final king capture");
    assert_true(game.winner().has_value() && *game.winner() == 0, "player 1 should win");
}

void test_castling() {
    eightd::GameState game(2);
    game.clear_for_testing();
    game.set_current_player(0);
    game.set_piece({4, 0}, eightd::Piece{.type = eightd::PieceType::King, .owner = 0, .pawn_direction = {}, .original_owner = 0, .move_count = 0});
    game.set_piece({7, 0}, eightd::Piece{.type = eightd::PieceType::Rook, .owner = 0, .pawn_direction = {}, .original_owner = 0, .move_count = 0});
    game.set_piece({4, 7}, eightd::Piece{.type = eightd::PieceType::King, .owner = 1, .pawn_direction = {}, .original_owner = 1, .move_count = 0});
    const auto moves = game.legal_moves_from({4, 0});
    bool has_castle = false;
    for (const auto& move : moves) {
        if (move.to == eightd::Coordinate{6, 0} && move.is_castle) {
            has_castle = true;
        }
    }
    assert_true(has_castle, "king should be able to castle kingside");
    assert_true(game.apply_move({4, 0}, {6, 0}), "castling move should apply");
    assert_true(game.pieces().contains("6,0"), "king should land on castled square");
    assert_true(game.pieces().contains("5,0"), "rook should land next to king");
}

void test_en_passant() {
    eightd::GameState game(2);
    game.clear_for_testing();
    game.set_current_player(1);
    game.set_piece({4, 0}, eightd::Piece{.type = eightd::PieceType::King, .owner = 0, .pawn_direction = {}, .original_owner = 0});
    game.set_piece({4, 7}, eightd::Piece{.type = eightd::PieceType::King, .owner = 1, .pawn_direction = {}, .original_owner = 1});
    game.set_piece({3, 4}, eightd::Piece{.type = eightd::PieceType::Pawn, .owner = 0, .pawn_direction = {0, 1}, .original_owner = 0});
    game.set_piece({4, 6}, eightd::Piece{.type = eightd::PieceType::Pawn, .owner = 1, .pawn_direction = {0, -1}, .original_owner = 1});
    assert_true(game.apply_move({4, 6}, {4, 4}), "double pawn move should apply");
    const auto moves = game.legal_moves_from({3, 4});
    bool has_en_passant = false;
    for (const auto& move : moves) {
        if (move.to == eightd::Coordinate{4, 5} && move.is_en_passant) {
            has_en_passant = true;
        }
    }
    assert_true(has_en_passant, "adjacent diagonal pawn should gain en passant capture");
    assert_true(game.apply_move({3, 4}, {4, 5}), "en passant should apply");
    assert_true(!game.pieces().contains("4,4"), "captured pawn should be removed from passed destination");
}

void test_check_filtering() {
    eightd::GameState game(2);
    game.clear_for_testing();
    game.set_current_player(0);
    game.set_piece({4, 0}, eightd::Piece{.type = eightd::PieceType::King, .owner = 0, .pawn_direction = {}, .original_owner = 0});
    game.set_piece({4, 1}, eightd::Piece{.type = eightd::PieceType::Rook, .owner = 0, .pawn_direction = {}, .original_owner = 0});
    game.set_piece({7, 7}, eightd::Piece{.type = eightd::PieceType::King, .owner = 1, .pawn_direction = {}, .original_owner = 1});
    game.set_piece({4, 7}, eightd::Piece{.type = eightd::PieceType::Rook, .owner = 1, .pawn_direction = {}, .original_owner = 1});
    const auto moves = game.legal_moves_from({4, 1});
    for (const auto& move : moves) {
        assert_true(move.to != eightd::Coordinate{5, 1}, "pinned rook must not expose its king to check");
    }
}

void test_simulation_runs() {
    eightd::SimulationRunner runner(42);
    const auto summary = runner.run_games(2, 2, 40);
    assert_true(summary.games == 2, "simulation should report correct game count");
}

void test_all_dimensions_initialize() {
    for (int dimensions = 2; dimensions <= 8; ++dimensions) {
        eightd::GameState game(dimensions);
        assert_true(game.dimensions() == dimensions, "game should remember chosen dimensions");
        assert_true(game.player_count() == (1 << (dimensions - 1)), "player count should scale by dimension");
    }
}

void test_evo_log_contract() {
    eightd::SimulationRunner runner(42);
    const eightd::GameLog log = runner.run_game_log(2, 8, false, 2, 3);
    const std::string json = eightd::serialize_game_log_json(log);
    assert_true(json.find("\"dimensions\":2") != std::string::npos, "game log should include dimensions");
    assert_true(json.find("\"research_checkpoints\"") != std::string::npos, "game log should include checkpoints");
    assert_true(json.find("\"player_roster\"") != std::string::npos, "game log should include player roster");
    assert_true(json.find("\"replay_events\"") != std::string::npos, "game log should include replay events");
    assert_true(json.find("\"replay_segments\"") != std::string::npos, "game log should include replay segments");
    assert_true(!log.research_checkpoints.empty(), "game log should produce checkpoints");
    assert_true(!log.research_checkpoints.front().state_json.empty(), "checkpoint should include serialized state");
    assert_true(!log.replay_segments.empty(), "game log should produce replay segments");
}

void test_evo_batch_contract() {
    eightd::SimulationRunner runner(42);
    eightd::EvoConfig config;
    config.dimensions = 2;
    config.generations = 1;
    config.games_per_generation = 1;
    config.max_moves = 6;
    config.checkpoint_interval = 2;
    config.replay_segment_span = 3;
    const eightd::EvoBatchResult batch = runner.run_evo_batch(config);
    const std::string json = eightd::serialize_evo_batch_json(batch);
    assert_true(batch.total_games == 1, "batch should run one game");
    assert_true(json.find("\"champions\"") != std::string::npos, "batch export should include champions");
    assert_true(json.find("\"runLogs\"") != std::string::npos, "batch export should include run logs");
}

void test_turn_advice_contract() {
    eightd::GameState game(2);
    eightd::SimulationRunner runner(42);
    const auto advice = runner.suggest_turn(game, game.current_player());
    const std::string json = eightd::serialize_turn_advice_json(advice);
    assert_true(advice.player_id == 0, "turn advice should target current player");
    assert_true(advice.has_move, "opening position should have an AI move");
    assert_true(json.find("\"hasMove\":true") != std::string::npos, "turn advice JSON should describe move availability");
}

}  // namespace

int main() {
    try {
        test_initial_setup();
        test_opening_pawn_move();
        test_turn_advances();
        test_promotion();
        test_king_capture();
        test_castling();
        test_en_passant();
        test_check_filtering();
        test_simulation_runs();
        test_all_dimensions_initialize();
        test_evo_log_contract();
        test_evo_batch_contract();
        test_turn_advice_contract();
    } catch (const std::exception& exception) {
        std::cerr << "TEST FAILURE: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All tests passed\n";
    return EXIT_SUCCESS;
}
