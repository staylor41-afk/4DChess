#include "eightd/engine.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

std::map<std::string, std::string> parse_args(int argc, char** argv) {
    std::map<std::string, std::string> args;
    for (int index = 1; index < argc; ++index) {
        const std::string token = argv[index];
        const auto separator = token.find('=');
        if (separator == std::string::npos) {
            args[token] = "true";
        } else {
            args[token.substr(0, separator)] = token.substr(separator + 1);
        }
    }
    return args;
}

int int_arg(const std::map<std::string, std::string>& args, const std::string& key, int fallback) {
    const auto iterator = args.find(key);
    return iterator == args.end() ? fallback : std::stoi(iterator->second);
}

std::uint64_t uint64_arg(const std::map<std::string, std::string>& args, const std::string& key, std::uint64_t fallback) {
    const auto iterator = args.find(key);
    return iterator == args.end() ? fallback : std::stoull(iterator->second);
}

bool bool_arg(const std::map<std::string, std::string>& args, const std::string& key, bool fallback) {
    const auto iterator = args.find(key);
    if (iterator == args.end()) {
        return fallback;
    }
    return iterator->second == "true" || iterator->second == "1" || iterator->second == "yes";
}

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  eightd_evo mode=run dimensions=2..8 generations=1 games=1 max_moves=200 until_winner=false checkpoint=50 segment=1000 seed=0 output=run.json champions=champions.json\n"
        << "  eightd_evo mode=single dimensions=2..8 max_moves=200 until_winner=false checkpoint=50 segment=1000 seed=0 output=run.json text_output=run.txt\n";
}

void maybe_write_file(const std::map<std::string, std::string>& args, const std::string& key, const std::string& body) {
    const auto iterator = args.find(key);
    if (iterator == args.end() || iterator->second.empty()) {
        return;
    }
    std::ofstream out(iterator->second, std::ios::binary);
    if (!out) {
        throw std::runtime_error("could not write " + iterator->second);
    }
    out << body;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace eightd;

    try {
        const auto args = parse_args(argc, argv);
        const std::string mode = args.contains("mode") ? args.at("mode") : "run";
        const int dimensions = int_arg(args, "dimensions", 2);
        if (dimensions < 2 || dimensions > 8) {
            throw std::invalid_argument("dimensions must be between 2 and 8");
        }

        const std::uint64_t seed = uint64_arg(args, "seed", 0);
        SimulationRunner runner(seed);

        if (mode == "single") {
            const GameLog log = runner.run_game_log(
                dimensions,
                int_arg(args, "max_moves", 200),
                bool_arg(args, "until_winner", false),
                int_arg(args, "checkpoint", 50),
                int_arg(args, "segment", 1000));
            const std::string json = serialize_game_log_json(log);
            maybe_write_file(args, "output", json);
            maybe_write_file(args, "text_output", serialize_game_log_text(log));
            std::cout << json << '\n';
            return EXIT_SUCCESS;
        }

        if (mode == "run") {
            EvoConfig config;
            config.dimensions = dimensions;
            config.generations = int_arg(args, "generations", 1);
            config.games_per_generation = int_arg(args, "games", 1);
            config.max_moves = int_arg(args, "max_moves", 200);
            config.until_winner = bool_arg(args, "until_winner", false);
            config.checkpoint_interval = int_arg(args, "checkpoint", 50);
            config.replay_segment_span = int_arg(args, "segment", 1000);
            config.seed = seed;
            const EvoBatchResult batch = runner.run_evo_batch(config);
            const std::string json = serialize_evo_batch_json(batch);
            maybe_write_file(args, "output", json);
            maybe_write_file(args,
                             "champions",
                             serialize_champion_export_json(batch.champions,
                                                            config.dimensions,
                                                            "headless-evo",
                                                            batch.run_logs.empty() ? "" : batch.run_logs.front().generated_at));
            std::cout << json << '\n';
            return EXIT_SUCCESS;
        }

        print_usage();
        return EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::cerr << "eightd_evo error: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
