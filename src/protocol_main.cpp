#include "eightd/engine.hpp"

#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>

namespace {

std::map<std::string, std::string> parse_kv(std::string_view line) {
    std::map<std::string, std::string> values;
    std::stringstream stream{std::string(line)};
    std::string token;
    while (stream >> token) {
        const auto separator = token.find('=');
        if (separator == std::string::npos) {
            values[token] = "";
        } else {
            values[token.substr(0, separator)] = token.substr(separator + 1);
        }
    }
    return values;
}

std::string parse_command(const std::string& line) {
    std::stringstream stream{line};
    std::string command;
    stream >> command;
    return command;
}

void print_error(std::string_view message) {
    std::cout << "{\"ok\":false,\"error\":\"" << message << "\"}" << std::endl;
}

}  // namespace

int main() {
    using namespace eightd;

    std::optional<GameState> game;
    std::string line;

    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }

        const auto tokens = parse_kv(line);
        const std::string command = parse_command(line);

        try {
            if (command == "ping") {
                std::cout << "{\"ok\":true,\"reply\":\"pong\"}" << std::endl;
            } else if (command == "new_game") {
                const int dimensions = std::stoi(tokens.at("dimensions"));
                game.emplace(dimensions);
                game->eliminate_if_no_legal_moves();
                std::cout << "{\"ok\":true,\"state\":" << game->serialize_state_json() << "}" << std::endl;
            } else if (command == "get_state") {
                if (!game.has_value()) {
                    print_error("no game");
                    continue;
                }
                std::cout << "{\"ok\":true,\"state\":" << game->serialize_state_json() << "}" << std::endl;
            } else if (command == "get_legal_moves") {
                if (!game.has_value()) {
                    print_error("no game");
                    continue;
                }
                const Coordinate from = parse_coordinate(tokens.at("from"));
                std::cout << "{\"ok\":true,\"moves\":" << game->serialize_moves_json(game->legal_moves_from(from)) << "}" << std::endl;
            } else if (command == "apply_move") {
                if (!game.has_value()) {
                    print_error("no game");
                    continue;
                }
                const Coordinate from = parse_coordinate(tokens.at("from"));
                const Coordinate to = parse_coordinate(tokens.at("to"));
                if (!game->apply_move(from, to)) {
                    print_error("illegal move");
                    continue;
                }
                std::cout << "{\"ok\":true,\"state\":" << game->serialize_state_json() << "}" << std::endl;
            } else if (command == "get_turn_analysis") {
                if (!game.has_value()) {
                    print_error("no game");
                    continue;
                }
                const int player_id = tokens.find("player") != tokens.end()
                    ? std::stoi(tokens.at("player"))
                    : game->current_player();
                SimulationRunner runner(tokens.find("seed") != tokens.end() ? std::stoull(tokens.at("seed")) : 0);
                const TurnAdvice advice = runner.suggest_turn(*game, player_id);
                std::cout << "{\"ok\":true,\"analysis\":" << serialize_turn_advice_json(advice) << "}" << std::endl;
            } else if (command == "get_slice_view") {
                if (!game.has_value()) {
                    print_error("no game");
                    continue;
                }
                const auto view_it = tokens.find("view");
                const auto fixed_it = tokens.find("fixed");
                const SliceRequest request = parse_slice_request(
                    view_it == tokens.end() ? "" : view_it->second,
                    fixed_it == tokens.end() ? "" : fixed_it->second);
                std::cout << "{\"ok\":true,\"slice\":" << game->serialize_slice_json(request) << "}" << std::endl;
            } else if (command == "suggest_ai_turn") {
                if (!game.has_value()) {
                    print_error("no game");
                    continue;
                }
                const int player_id = tokens.find("player") != tokens.end()
                    ? std::stoi(tokens.at("player"))
                    : game->current_player();
                const std::uint64_t seed = tokens.find("seed") != tokens.end() ? std::stoull(tokens.at("seed")) : 0;
                SimulationRunner runner(seed);
                const TurnAdvice advice = runner.suggest_turn(*game, player_id);
                std::cout << "{\"ok\":true,\"turn\":" << serialize_turn_advice_json(advice) << "}" << std::endl;
            } else if (command == "apply_ai_turn") {
                if (!game.has_value()) {
                    print_error("no game");
                    continue;
                }
                const int player_id = tokens.find("player") != tokens.end()
                    ? std::stoi(tokens.at("player"))
                    : game->current_player();
                const std::uint64_t seed = tokens.find("seed") != tokens.end() ? std::stoull(tokens.at("seed")) : 0;
                SimulationRunner runner(seed);
                const TurnAdvice advice = runner.suggest_turn(*game, player_id);
                if (!advice.has_move || !game->apply_move(advice.move.from, advice.move.to)) {
                    print_error("no legal ai move");
                    continue;
                }
                std::cout << "{\"ok\":true,\"turn\":" << serialize_turn_advice_json(advice)
                          << ",\"state\":" << game->serialize_state_json() << "}" << std::endl;
            } else if (command == "simulate_games") {
                const int dimensions = std::stoi(tokens.at("dimensions"));
                const int count = std::stoi(tokens.at("count"));
                const int max_moves = std::stoi(tokens.at("max_moves"));
                const std::uint64_t seed = tokens.find("seed") != tokens.end() ? std::stoull(tokens.at("seed")) : 0;
                SimulationRunner runner(seed);
                const SimulationSummary summary = runner.run_games(dimensions, count, max_moves);
                std::cout << "{\"ok\":true,\"summary\":{"
                          << "\"dimensions\":" << summary.dimensions << ","
                          << "\"games\":" << summary.games << ","
                          << "\"maxMoves\":" << summary.max_moves << ","
                          << "\"totalMoves\":" << summary.total_moves << ","
                          << "\"winsByPlayer\":{";
                bool first = true;
                for (const auto& [player_id, wins] : summary.wins_by_player) {
                    if (!first) {
                        std::cout << ",";
                    }
                    first = false;
                    std::cout << "\"" << player_id << "\":" << wins;
                }
                std::cout << "}}}" << std::endl;
            } else if (command == "quit" || command == "exit") {
                std::cout << "{\"ok\":true,\"reply\":\"bye\"}" << std::endl;
                return 0;
            } else {
                print_error("unknown command");
            }
        } catch (const std::exception& exception) {
            print_error(exception.what());
        }
    }

    return 0;
}
