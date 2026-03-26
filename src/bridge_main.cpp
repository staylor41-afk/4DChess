#ifdef _WIN32

#include "eightd/engine.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <sstream>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace {

struct BridgeMessage {
    int turn = 0;
    int sender = -1;
    std::vector<int> recipients {};
    bool is_public = true;
    std::string mode = "human";
    std::string content {};
    int target = -1;
    std::string intent {};
};

struct BridgeFocus {
    int target = -1;
    int until = 0;
};

struct BridgeDiplomacy {
    bool enabled = false;
    std::string mode = "human";
    int turn = 0;
    std::vector<std::vector<double>> trust {};
    std::map<std::string, int> truces {};
    std::vector<BridgeFocus> focus {};
    std::vector<BridgeMessage> messages {};
};

std::string decode_query_component(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '+') {
            out.push_back(' ');
        } else if (ch == '%' && index + 2 < text.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                return -1;
            };
            const int high = hex(text[index + 1]);
            const int low = hex(text[index + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char>((high << 4) | low));
                index += 2;
            } else {
                out.push_back(ch);
            }
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::map<std::string, std::string> parse_query(const std::string& query) {
    std::map<std::string, std::string> values;
    std::stringstream stream(query);
    std::string token;
    while (std::getline(stream, token, '&')) {
        const auto separator = token.find('=');
        if (separator == std::string::npos) {
            values[token] = "";
        } else {
            values[token.substr(0, separator)] = decode_query_component(token.substr(separator + 1));
        }
    }
    return values;
}

struct ProjectionConfig {
    int dimensions = 2;
    int board_size = 8;
    int canvas_width = 1280;
    int canvas_height = 720;
    double zoom = 1.0;
    double pan_x = 0.0;
    double pan_y = 0.0;
    double rot_x = 0.0;
    double rot_y = 0.0;
    double building_rot_x = 0.0;
    double building_rot_y = 0.0;
    double city_rot_x = 0.0;
    double city_rot_y = 0.0;
    double w_spread = 1.0;
    double v_spread = 1.0;
    double u_spread = 1.0;
    double t_spread = 1.0;
    double s_spread = 1.0;
};

struct ProjectedCell {
    eightd::Coordinate coord {};
    double depth = 0.0;
    double screen_x = 0.0;
    double screen_y = 0.0;
};

struct ProjectedPoint {
    double world_x = 0.0;
    double world_y = 0.0;
    double depth = 0.0;
    double screen_x = 0.0;
    double screen_y = 0.0;
};

double query_double(const std::map<std::string, std::string>& args, std::string_view key, double fallback) {
    const auto iterator = args.find(std::string(key));
    if (iterator == args.end()) {
        return fallback;
    }
    try {
        return std::stod(iterator->second);
    } catch (...) {
        return fallback;
    }
}

int query_int(const std::map<std::string, std::string>& args, std::string_view key, int fallback) {
    const auto iterator = args.find(std::string(key));
    if (iterator == args.end()) {
        return fallback;
    }
    try {
        return std::stoi(iterator->second);
    } catch (...) {
        return fallback;
    }
}

ProjectionConfig parse_projection_config(const eightd::GameState& game, const std::map<std::string, std::string>& args) {
    ProjectionConfig config;
    config.dimensions = game.dimensions();
    config.board_size = game.board_size();
    config.canvas_width = (std::max)(320, query_int(args, "width", config.canvas_width));
    config.canvas_height = (std::max)(240, query_int(args, "height", config.canvas_height));
    config.zoom = query_double(args, "zoom", config.zoom);
    config.pan_x = query_double(args, "panX", config.pan_x);
    config.pan_y = query_double(args, "panY", config.pan_y);
    config.rot_x = query_double(args, "rotX", config.rot_x);
    config.rot_y = query_double(args, "rotY", config.rot_y);
    config.building_rot_x = query_double(args, "bldRotX", config.building_rot_x);
    config.building_rot_y = query_double(args, "bldRotY", config.building_rot_y);
    config.city_rot_x = query_double(args, "cityRotX", config.city_rot_x);
    config.city_rot_y = query_double(args, "cityRotY", config.city_rot_y);
    config.w_spread = query_double(args, "wSpread", config.w_spread);
    config.v_spread = query_double(args, "vSpread", config.v_spread);
    config.u_spread = query_double(args, "uSpread", config.u_spread);
    config.t_spread = query_double(args, "tSpread", config.t_spread);
    config.s_spread = query_double(args, "sSpread", config.s_spread);
    return config;
}

ProjectedPoint project_point(const ProjectionConfig& config, double col, double row, double z, double w, double v, double u, double t, double s) {
    const double cx = col - 3.5;
    const double cy = row - 3.5;
    const double cz = z - 2.0;
    const double cos_y = std::cos(config.rot_y);
    const double sin_y = std::sin(config.rot_y);
    const double rx1 = cx * cos_y + cy * sin_y;
    const double ry1 = -cx * sin_y + cy * cos_y;
    const double rz1 = cz;
    const double cos_x = std::cos(config.rot_x);
    const double sin_x = std::sin(config.rot_x);
    const double rx2 = rx1;
    const double ry2 = ry1 * cos_x - rz1 * sin_x;
    const double rz2 = ry1 * sin_x + rz1 * cos_x;
    constexpr double scale = 14.0;
    const double px2 = rx2 * scale;
    const double py2 = ry2 * scale;

    double world_x = px2;
    double world_y = py2;
    double depth = rz2;
    const double half = (config.board_size - 1) / 2.0;

    if (config.dimensions == 2) {
        world_x = (col - 3.5) * 18.0;
        world_y = (row - 3.5) * 18.0;
        depth = 0.0;
    } else if (config.dimensions == 3) {
        // Already in px2/py2/rz2 space.
    } else if (config.dimensions == 5) {
        const double cube_spacing_x = (config.board_size + 1) * scale * 1.05;
        const double cube_spacing_y = (config.board_size + 1) * scale * 1.1;
        world_x += (w - half) * cube_spacing_x;
        world_y += (v - half) * cube_spacing_y;
    } else if (config.dimensions == 6) {
        const double s6 = 14.0 * 1.05;
        const double cube_spacing_x = (config.board_size + 1) * s6 * config.w_spread;
        const double cube_spacing_y = (config.board_size + 1) * s6 * 1.08 * config.v_spread;
        const double board_spacing_u = (config.board_size + 1) * s6 * 1.55 * config.u_spread;
        const double mw = w - half;
        const double mv = v - half;
        const double mu = u - half;
        const double ox = mw * cube_spacing_x;
        const double oy = mv * cube_spacing_y;
        const double uox = mu * board_spacing_u * 0.36;
        const double uoy = mu * board_spacing_u * (-0.78);
        world_x += ox + uox;
        world_y += oy + uoy;
        depth += mu * 26.0;
    } else if (config.dimensions == 7 || config.dimensions == 8) {
        const double s6 = 14.0 * 1.05;
        const double cube_spacing_x = (config.board_size + 1) * s6 * config.w_spread;
        const double cube_spacing_y = (config.board_size + 1) * s6 * 1.05 * config.v_spread;
        const double cube_spacing_u = (config.board_size + 1) * s6 * config.u_spread;
        const double mw = w - half;
        const double mv = v - half;
        const double mu = u - half;
        const double cb_y = std::cos(config.building_rot_y);
        const double sb_y = std::sin(config.building_rot_y);
        const double cb_x = std::cos(config.building_rot_x);
        const double sb_x = std::sin(config.building_rot_x);
        const double mr1 = mw * cb_y + mv * sb_y;
        const double mv1 = -mw * sb_y + mv * cb_y;
        const double mu1 = mu;
        const double mr2 = mr1;
        const double mv2 = mv1 * cb_x - mu1 * sb_x;
        const double mu2 = mv1 * sb_x + mu1 * cb_x;
        const double building_ox = mr2 * cube_spacing_x + mu2 * cube_spacing_u * 0.18;
        const double building_oy = mv2 * cube_spacing_y + mu2 * cube_spacing_u * (-0.15);
        const double building_depth = mu2 * 8.0;

        const double cos_yaw = std::cos(config.city_rot_y);
        const double sin_yaw = std::sin(config.city_rot_y);
        const double cos_tilt = std::cos(config.city_rot_x);
        const double sin_tilt = std::sin(config.city_rot_x);

        const double major_span = (cube_spacing_x * 7.2 + cube_spacing_u * 1.0);
        const double minor_span = (cube_spacing_y * 4.7 + cube_spacing_u * 1.25);
        const double t_span = major_span * config.t_spread;
        const double s_span = (config.dimensions == 8 ? major_span * 0.82 : major_span * 0.78) * config.s_spread;

        const double t_dir_x = cos_yaw * t_span;
        const double t_dir_y = (sin_yaw * t_span * 0.34 + minor_span) * cos_tilt;
        const double t_depth = (minor_span + std::abs(sin_yaw) * t_span * 0.08) * sin_tilt * 0.85;

        const double s_dir_x = (-sin_yaw * s_span * 0.92);
        const double s_dir_y = (cos_yaw * s_span * 0.30 + minor_span * 1.10) * cos_tilt;
        const double s_depth = (minor_span * 1.05 + std::abs(cos_yaw) * s_span * 0.06) * sin_tilt * 0.65;

        const double mt = t - half;
        const double ms = (config.dimensions == 8 ? s - half : 0.0);
        const double city_x = mt * t_dir_x + ms * s_dir_x;
        const double city_y = mt * t_dir_y + ms * s_dir_y;
        const double city_depth = mt * t_depth + ms * s_depth;
        world_x += building_ox + city_x;
        world_y += building_oy + city_y;
        depth += building_depth + city_depth;
    } else {
        const double cube_spacing_x = (config.board_size + 1) * scale * 1.05;
        world_x += (w - half) * cube_spacing_x;
    }

    return ProjectedPoint{world_x, world_y, depth, world_x * config.zoom + config.pan_x, world_y * config.zoom + config.pan_y};
}

ProjectedCell project_cell(const ProjectionConfig& config, const eightd::Coordinate& coord) {
    const auto at = [&](std::size_t index) -> double {
        return index < coord.size() ? static_cast<double>(coord[index]) : 0.0;
    };
    const auto projected = project_point(config, at(0), at(1), at(2), at(3), at(4), at(5), at(6), at(7));
    return ProjectedCell{coord, projected.depth, projected.screen_x, projected.screen_y};
}

std::string visible_cells_json(const eightd::GameState& game, const ProjectionConfig& config) {
    const int size = game.board_size();
    const int dimensions = game.dimensions();
    const int w_max = (dimensions == 2 || dimensions == 3) ? 1 : size;
    const int z_max = (dimensions == 2) ? 1 : size;
    const int v_max = dimensions >= 5 ? size : 1;
    const int u_max = dimensions >= 6 ? size : 1;
    const int t_max = dimensions >= 7 ? size : 1;
    const int s_max = dimensions >= 8 ? size : 1;
    const bool is_high_d = dimensions >= 7;
    const double margin = 140.0 * config.zoom;
    std::vector<ProjectedCell> cells;
    cells.reserve(4096);

    if (is_high_d) {
        for (int s = 0; s < s_max; ++s) {
            for (int t = 0; t < t_max; ++t) {
                const auto building_center = project_point(config, 3.5, 3.5, 3.5, 3.5, 3.5, 3.5, static_cast<double>(t), static_cast<double>(s));
                const double building_radius =
                    (size + 1) * 14.0 * 1.05 * (std::max)({config.w_spread, config.v_spread, config.u_spread}) * config.zoom * 8.0;
                if (building_center.screen_x + building_radius < 0.0 ||
                    building_center.screen_x - building_radius > static_cast<double>(config.canvas_width) ||
                    building_center.screen_y + building_radius < 0.0 ||
                    building_center.screen_y - building_radius > static_cast<double>(config.canvas_height)) {
                    continue;
                }
                for (int u = 0; u < u_max; ++u) {
                    for (int v = 0; v < v_max; ++v) {
                        for (int w = 0; w < w_max; ++w) {
                            const auto cube_center = project_point(
                                config,
                                3.5,
                                3.5,
                                3.5,
                                static_cast<double>(w),
                                static_cast<double>(v),
                                static_cast<double>(u),
                                static_cast<double>(t),
                                static_cast<double>(s));
                            const double cube_radius = (size + 1) * 14.0 * 1.12 * config.zoom * 1.9;
                            if (cube_center.screen_x + cube_radius < -margin ||
                                cube_center.screen_x - cube_radius > static_cast<double>(config.canvas_width) + margin ||
                                cube_center.screen_y + cube_radius < -margin ||
                                cube_center.screen_y - cube_radius > static_cast<double>(config.canvas_height) + margin) {
                                continue;
                            }
                            for (int z = 0; z < z_max; ++z) {
                                for (int row = 0; row < size; ++row) {
                                    for (int col = 0; col < size; ++col) {
                                        eightd::Coordinate coord {col, row, z, w, v, u, t, s};
                                        auto projected = project_cell(config, coord);
                                        if (projected.screen_x < -margin || projected.screen_x > static_cast<double>(config.canvas_width) + margin ||
                                            projected.screen_y < -margin || projected.screen_y > static_cast<double>(config.canvas_height) + margin) {
                                            continue;
                                        }
                                        cells.push_back(std::move(projected));
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        for (int s = 0; s < s_max; ++s) {
            for (int t = 0; t < t_max; ++t) {
                for (int u = 0; u < u_max; ++u) {
                    for (int v = 0; v < v_max; ++v) {
                        for (int w = 0; w < w_max; ++w) {
                            for (int z = 0; z < z_max; ++z) {
                                for (int row = 0; row < size; ++row) {
                                    for (int col = 0; col < size; ++col) {
                                        eightd::Coordinate coord {col, row, z, w, v, u, t, s};
                                        cells.push_back(project_cell(config, coord));
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    std::sort(cells.begin(), cells.end(), [](const ProjectedCell& left, const ProjectedCell& right) {
        return left.depth < right.depth;
    });

    std::ostringstream output;
    output << "{"
           << "\"dimensions\":" << dimensions << ","
           << "\"count\":" << cells.size() << ","
           << "\"cells\":[";
    for (std::size_t index = 0; index < cells.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << "{"
               << "\"coords\":[";
        for (std::size_t coord_index = 0; coord_index < cells[index].coord.size(); ++coord_index) {
            if (coord_index != 0) {
                output << ",";
            }
            output << cells[index].coord[coord_index];
        }
        output << "],"
               << "\"depth\":" << cells[index].depth << ","
               << "\"sx\":" << cells[index].screen_x << ","
               << "\"sy\":" << cells[index].screen_y
               << "}";
    }
    output << "]}";
    return output.str();
}

std::string json_error(std::string_view message) {
    return std::string("{\"ok\":false,\"error\":\"") + std::string(message) + "\"}";
}

void send_json(SOCKET client, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Headers: *\r\n"
             << "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
             << "Content-Length: " << body.size() << "\r\n\r\n"
             << body;
    const std::string text = response.str();
    send(client, text.c_str(), static_cast<int>(text.size()), 0);
}

void send_options(SOCKET client) {
    const std::string text =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Content-Length: 0\r\n\r\n";
    send(client, text.c_str(), static_cast<int>(text.size()), 0);
}

double clamp_trust(double value) {
    return (std::max)(-1.0, (std::min)(1.0, value));
}

std::string coord_key(const eightd::Coordinate& coordinate) {
    std::ostringstream output;
    for (std::size_t index = 0; index < coordinate.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << coordinate[index];
    }
    return output.str();
}

std::string truce_key(int a, int b) {
    const int low = (std::min)(a, b);
    const int high = (std::max)(a, b);
    return std::to_string(low) + "|" + std::to_string(high);
}

std::vector<int> alive_players(const eightd::GameState& game) {
    std::vector<int> players;
    for (int player_id = 0; player_id < game.player_count(); ++player_id) {
        if (game.has_king(player_id)) {
            players.push_back(player_id);
        }
    }
    return players;
}

int board_leader(const eightd::GameState& game) {
    int leader = -1;
    int best = -1;
    for (int player_id = 0; player_id < game.player_count(); ++player_id) {
        if (!game.has_king(player_id)) {
            continue;
        }
        const int pieces = game.piece_count(player_id);
        if (pieces > best) {
            best = pieces;
            leader = player_id;
        }
    }
    return leader;
}

void init_bridge_diplomacy(BridgeDiplomacy& diplomacy, const eightd::GameState& game, bool enabled, const std::string& mode) {
    diplomacy.enabled = enabled;
    diplomacy.mode = mode.empty() ? "human" : mode;
    diplomacy.turn = 0;
    diplomacy.messages.clear();
    diplomacy.truces.clear();
    diplomacy.trust.assign(game.player_count(), std::vector<double>(game.player_count(), 0.22));
    diplomacy.focus.assign(game.player_count(), BridgeFocus{});
    for (int player_id = 0; player_id < game.player_count(); ++player_id) {
        diplomacy.trust[player_id][player_id] = 0.0;
    }
}

void emit_bridge_message(BridgeDiplomacy& diplomacy,
                         int sender,
                         std::vector<int> recipients,
                         bool is_public,
                         std::string content,
                         int target,
                         std::string intent) {
    if (!diplomacy.enabled || sender < 0) {
        return;
    }
    diplomacy.messages.push_back(BridgeMessage{
        .turn = diplomacy.turn,
        .sender = sender,
        .recipients = std::move(recipients),
        .is_public = is_public,
        .mode = diplomacy.mode,
        .content = std::move(content),
        .target = target,
        .intent = std::move(intent)
    });
    if (diplomacy.messages.size() > 240) {
        diplomacy.messages.erase(diplomacy.messages.begin(), diplomacy.messages.begin() + static_cast<std::ptrdiff_t>(diplomacy.messages.size() - 240));
    }
}

void set_truce(BridgeDiplomacy& diplomacy, int a, int b, int until_turn) {
    if (a < 0 || b < 0 || a == b) {
        return;
    }
    diplomacy.truces[truce_key(a, b)] = until_turn;
    diplomacy.trust[a][b] = clamp_trust(diplomacy.trust[a][b] + 0.06);
    diplomacy.trust[b][a] = clamp_trust(diplomacy.trust[b][a] + 0.06);
}

bool has_live_truce(const BridgeDiplomacy& diplomacy, int a, int b) {
    const auto iterator = diplomacy.truces.find(truce_key(a, b));
    return iterator != diplomacy.truces.end() && iterator->second >= diplomacy.turn;
}

void decay_truces(BridgeDiplomacy& diplomacy) {
    for (auto iterator = diplomacy.truces.begin(); iterator != diplomacy.truces.end();) {
        if (iterator->second < diplomacy.turn) {
            iterator = diplomacy.truces.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void update_bridge_diplomacy_after_ai_turn(BridgeDiplomacy& diplomacy,
                                           const eightd::GameState& game,
                                           int actor,
                                           std::optional<int> victim) {
    if (!diplomacy.enabled || actor < 0 || actor >= game.player_count()) {
        return;
    }
    diplomacy.turn += 1;
    decay_truces(diplomacy);

    const auto alive = alive_players(game);
    const int leader = board_leader(game);
    const int actor_pieces = game.piece_count(actor);
    const int leader_pieces = leader >= 0 ? game.piece_count(leader) : actor_pieces;

    if (victim.has_value() && has_live_truce(diplomacy, actor, *victim)) {
        diplomacy.truces.erase(truce_key(actor, *victim));
        for (int observer : alive) {
            if (observer == actor) {
                continue;
            }
            diplomacy.trust[observer][actor] = clamp_trust(diplomacy.trust[observer][actor] - (observer == *victim ? 0.32 : 0.14));
        }
        emit_bridge_message(diplomacy,
                            actor,
                            {},
                            true,
                            "I broke ranks with P" + std::to_string(*victim + 1) + ". Watch me if you must.",
                            *victim,
                            "betrayal");
        return;
    }

    if (game.is_in_check(actor)) {
        const int target = (leader >= 0 && leader != actor) ? leader : -1;
        std::string text = "I am under pressure.";
        if (target >= 0) {
            text += " Give me room and strike P" + std::to_string(target + 1) + ".";
        }
        emit_bridge_message(diplomacy, actor, {}, true, text, target, "mercy");
        return;
    }

    if (leader >= 0 && leader != actor && leader_pieces >= actor_pieces + 2 && ((diplomacy.turn + actor) % 3 == 0)) {
        for (int player_id : alive) {
            if (player_id != leader) {
                diplomacy.focus[player_id] = BridgeFocus{leader, diplomacy.turn + 4};
            }
        }
        emit_bridge_message(diplomacy,
                            actor,
                            {},
                            true,
                            "P" + std::to_string(leader + 1) + " is getting ahead. Pressure them now.",
                            leader,
                            "dogpile");
        return;
    }

    int buddy = -1;
    double best_trust = -2.0;
    for (int other : alive) {
        if (other == actor || other == leader) {
            continue;
        }
        if (diplomacy.trust[actor][other] > best_trust) {
            best_trust = diplomacy.trust[actor][other];
            buddy = other;
        }
    }

    if (buddy >= 0 && best_trust > 0.20 && ((diplomacy.turn + actor + buddy) % 2 == 0)) {
        set_truce(diplomacy, actor, buddy, diplomacy.turn + 6);
        diplomacy.focus[actor] = BridgeFocus{leader >= 0 && leader != actor ? leader : buddy, diplomacy.turn + 4};
        emit_bridge_message(diplomacy,
                            actor,
                            {buddy},
                            false,
                            "P" + std::to_string(buddy + 1) + ", spare my line and I will spare yours.",
                            leader >= 0 && leader != actor ? leader : -1,
                            "truce");
        return;
    }

    if (leader >= 0 && leader != actor && ((diplomacy.turn + actor) % 4 == 1)) {
        emit_bridge_message(diplomacy,
                            actor,
                            {},
                            true,
                            "P" + std::to_string(leader + 1) + " looks exposed. I am shifting toward them.",
                            leader,
                            "warning");
    }
}

std::string player_summaries_json(const eightd::GameState& game) {
    std::ostringstream output;
    output << "[";
    for (int player_id = 0; player_id < game.player_count(); ++player_id) {
        if (player_id != 0) {
            output << ",";
        }
        output << "{"
               << "\"playerId\":" << player_id << ","
               << "\"alive\":" << (game.has_king(player_id) ? "true" : "false") << ","
               << "\"pieceCount\":" << game.piece_count(player_id) << ","
               << "\"isCurrent\":" << (game.current_player() == player_id ? "true" : "false")
               << "}";
    }
    output << "]";
    return output.str();
}

std::string coord_json(const eightd::Coordinate& coordinate) {
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

std::string bridge_piece_type_name(eightd::PieceType type) {
    using eightd::PieceType;
    switch (type) {
        case PieceType::King: return "king";
        case PieceType::Queen: return "queen";
        case PieceType::Rook: return "rook";
        case PieceType::Bishop: return "bishop";
        case PieceType::Knight: return "knight";
        case PieceType::Pawn: return "pawn";
    }
    return "pawn";
}

std::string player_intel_json(const eightd::GameState& game, int player_id) {
    using namespace eightd;
    std::vector<std::pair<Coordinate, Piece>> pieces;
    for (const auto& [key, piece] : game.pieces()) {
        if (piece.owner != player_id) {
            continue;
        }
        pieces.push_back({parse_coordinate(key), piece});
    }
    const auto piece_order = [](PieceType type) {
        switch (type) {
            case PieceType::King: return 0;
            case PieceType::Queen: return 1;
            case PieceType::Rook: return 2;
            case PieceType::Bishop: return 3;
            case PieceType::Knight: return 4;
            case PieceType::Pawn: return 5;
        }
        return 9;
    };
    std::sort(pieces.begin(), pieces.end(), [&](const auto& left, const auto& right) {
        const int left_rank = piece_order(left.second.type);
        const int right_rank = piece_order(right.second.type);
        if (left_rank != right_rank) {
            return left_rank < right_rank;
        }
        return left.first < right.first;
    });

    std::ostringstream output;
    output << "{"
           << "\"playerId\":" << player_id << ","
           << "\"alive\":" << (game.has_king(player_id) ? "true" : "false") << ","
           << "\"pieceCount\":" << game.piece_count(player_id) << ","
           << "\"inCheck\":" << (game.is_in_check(player_id) ? "true" : "false") << ","
           << "\"legalMoveCount\":" << game.legal_moves_for_player(player_id).size() << ","
           << "\"pieces\":[";
    for (std::size_t index = 0; index < pieces.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << "{"
               << "\"coord\":" << coord_json(pieces[index].first) << ","
               << "\"type\":\"" << bridge_piece_type_name(pieces[index].second.type) << "\""
               << "}";
    }
    output << "]}";
    return output.str();
}

std::string diplomacy_snapshot_json(const BridgeDiplomacy& diplomacy, const eightd::GameState& game) {
    std::ostringstream output;
    output << "{"
           << "\"enabled\":" << (diplomacy.enabled ? "true" : "false") << ","
           << "\"mode\":\"" << diplomacy.mode << "\","
           << "\"turn\":" << diplomacy.turn << ","
           << "\"messageCount\":" << diplomacy.messages.size();
    if (!diplomacy.enabled) {
        output << ",\"messages\":[],\"truces\":[],\"trustPairs\":[],\"focus\":[]}";
        return output.str();
    }
    output << ",\"messages\":[";
    for (std::size_t index = 0; index < diplomacy.messages.size(); ++index) {
        const auto& message = diplomacy.messages[index];
        if (index != 0) {
            output << ",";
        }
        output << "{"
               << "\"turn\":" << message.turn << ","
               << "\"sender\":" << message.sender << ","
               << "\"recipients\":[";
        for (std::size_t recipient_index = 0; recipient_index < message.recipients.size(); ++recipient_index) {
            if (recipient_index != 0) {
                output << ",";
            }
            output << message.recipients[recipient_index];
        }
        output << "],"
               << "\"isPublic\":" << (message.is_public ? "true" : "false") << ","
               << "\"mode\":\"" << message.mode << "\","
               << "\"content\":\"";
        for (const char ch : message.content) {
            if (ch == '"' || ch == '\\') {
                output << '\\';
            }
            output << ch;
        }
        output << "\","
               << "\"target\":" << message.target << ","
               << "\"intent\":\"" << message.intent << "\""
               << "}";
    }
    output << "],\"truces\":[";
    bool first = true;
    for (const auto& [key, until] : diplomacy.truces) {
        if (until < diplomacy.turn) {
            continue;
        }
        const auto split = key.find('|');
        if (split == std::string::npos) {
            continue;
        }
        if (!first) {
            output << ",";
        }
        first = false;
        output << "{"
               << "\"a\":" << std::stoi(key.substr(0, split)) << ","
               << "\"b\":" << std::stoi(key.substr(split + 1)) << ","
               << "\"until\":" << until
               << "}";
    }
    output << "],\"trustPairs\":[";
    std::vector<std::tuple<int, int, double>> trust_pairs;
    trust_pairs.reserve(static_cast<std::size_t>(game.player_count()));
    for (int a = 0; a < game.player_count(); ++a) {
        for (int b = a + 1; b < game.player_count(); ++b) {
            const double trust_value = (diplomacy.trust[a][b] + diplomacy.trust[b][a]) / 2.0;
            if (trust_value < 0.4) {
                continue;
            }
            trust_pairs.emplace_back(a, b, trust_value);
        }
    }
    std::sort(trust_pairs.begin(), trust_pairs.end(), [](const auto& lhs, const auto& rhs) {
        return std::get<2>(lhs) > std::get<2>(rhs);
    });
    if (trust_pairs.size() > 48) {
        trust_pairs.resize(48);
    }
    bool trust_first = true;
    for (const auto& [a, b, trust_value] : trust_pairs) {
        if (!trust_first) {
            output << ",";
        }
        trust_first = false;
        output << "{"
               << "\"a\":" << a << ","
               << "\"b\":" << b << ","
               << "\"v\":" << trust_value
               << "}";
    }
    output << "],\"focus\":[";
    bool focus_first = true;
    for (int pid = 0; pid < static_cast<int>(diplomacy.focus.size()); ++pid) {
        const auto& focus = diplomacy.focus[pid];
        if (focus.target < 0 || focus.until < diplomacy.turn) {
            continue;
        }
        if (!focus_first) {
            output << ",";
        }
        focus_first = false;
        output << "{"
               << "\"pid\":" << pid << ","
               << "\"target\":" << focus.target << ","
               << "\"until\":" << focus.until
               << "}";
    }
    output << "]}";
    return output.str();
}

}  // namespace

int main(int argc, char** argv) {
    using namespace eightd;

    const int port = argc > 1 ? std::atoi(argv[1]) : 8765;
    std::optional<GameState> game;
    std::optional<BridgeDiplomacy> diplomacy;
    WSADATA wsa_data {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "WSAStartup failed\n";
        return EXIT_FAILURE;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::cerr << "socket failed\n";
        WSACleanup();
        return EXIT_FAILURE;
    }

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(server, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "bind/listen failed\n";
        closesocket(server);
        WSACleanup();
        return EXIT_FAILURE;
    }

    std::cout << "eightd_bridge listening on http://127.0.0.1:" << port << "\n";

    while (true) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            continue;
        }

        char buffer[8192];
        const int received = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            closesocket(client);
            continue;
        }
        buffer[received] = '\0';
        const std::string request(buffer);
        if (request.rfind("OPTIONS ", 0) == 0) {
            send_options(client);
            closesocket(client);
            continue;
        }

        std::stringstream request_stream(request);
        std::string method;
        std::string target;
        std::string version;
        request_stream >> method >> target >> version;

        std::string path = target;
        std::string query;
        const auto query_pos = target.find('?');
        if (query_pos != std::string::npos) {
            path = target.substr(0, query_pos);
            query = target.substr(query_pos + 1);
        }
        const auto args = parse_query(query);

        try {
            if (path == "/ping") {
                send_json(client, "{\"ok\":true,\"reply\":\"pong\"}");
            } else if (path == "/new_game") {
                const int dimensions = args.contains("dimensions") ? std::stoi(args.at("dimensions")) : 2;
                const bool diplomacy_enabled = args.contains("diplomacy") && args.at("diplomacy") == "1";
                const std::string diplomacy_mode = args.contains("comm") ? args.at("comm") : "human";
                game.emplace(dimensions);
                diplomacy.emplace();
                init_bridge_diplomacy(*diplomacy, *game, diplomacy_enabled, diplomacy_mode);
                // Return minimal state stub — JS checks data.ok && data.state exist.
                // Full serialize_state_json for 128-player 8D is 5-10MB and causes multi-minute hangs.
                {
                    std::ostringstream resp;
                    resp << "{\"ok\":true,\"state\":{"
                         << "\"dimensions\":" << dimensions << ","
                         << "\"boardSize\":" << game->board_size() << ","
                         << "\"playerCount\":" << game->player_count() << ","
                         << "\"currentPlayer\":" << game->current_player() << ","
                         << "\"moveNumber\":0,"
                         << "\"gameOver\":false,"
                         << "\"winner\":null,"
                         << "\"inCheck\":false,"
                         << "\"activePlayers\":[";
                    for (int pi = 0; pi < game->player_count(); ++pi) {
                        if (pi > 0) resp << ",";
                        resp << pi;
                    }
                    resp << "],\"enPassantTarget\":null,\"pieces\":[]}}";
                    send_json(client, resp.str());
                }
            } else if (path == "/state") {
                if (!game.has_value()) {
                    send_json(client, json_error("no game"));
                } else {
                    send_json(client, std::string("{\"ok\":true,\"state\":") + game->serialize_state_json(false, false) + "}");
                }
            } else if (path == "/diplomacy_snapshot") {
                if (!game.has_value() || !diplomacy.has_value()) {
                    send_json(client, json_error("no game"));
                } else {
                    send_json(client, std::string("{\"ok\":true,\"diplomacy\":") + diplomacy_snapshot_json(*diplomacy, *game) + "}");
                }
            } else if (path == "/player_summaries") {
                if (!game.has_value()) {
                    send_json(client, json_error("no game"));
                } else {
                    send_json(client, std::string("{\"ok\":true,\"players\":") + player_summaries_json(*game) + "}");
                }
            } else if (path == "/player_intel") {
                if (!game.has_value()) {
                    send_json(client, json_error("no game"));
                } else {
                    const int player_id = args.contains("player") ? std::stoi(args.at("player")) : game->current_player();
                    send_json(client, std::string("{\"ok\":true,\"player\":") + player_intel_json(*game, player_id) + "}");
                }
            } else if (path == "/slice_view") {
                if (!game.has_value()) {
                    send_json(client, json_error("no game"));
                } else {
                    const std::string view_text = args.contains("view") ? args.at("view") : "0,1";
                    const std::string fixed_text = args.contains("fixed") ? args.at("fixed") : "";
                    const SliceRequest request = parse_slice_request(view_text, fixed_text);
                    send_json(client, std::string("{\"ok\":true,\"slice\":") + game->serialize_slice_json(request) + "}");
                }
            } else if (path == "/visible_cells") {
                if (!game.has_value()) {
                    send_json(client, json_error("no game"));
                } else {
                    const ProjectionConfig config = parse_projection_config(*game, args);
                    send_json(client, std::string("{\"ok\":true,\"visible\":") + visible_cells_json(*game, config) + "}");
                }
            } else if (path == "/legal_moves") {
                if (!game.has_value()) {
                    send_json(client, json_error("no game"));
                } else if (!args.contains("from")) {
                    send_json(client, json_error("missing from"));
                } else {
                    const Coordinate from = parse_coordinate(args.at("from"));
                    send_json(client, std::string("{\"ok\":true,\"moves\":") + game->serialize_moves_json(game->legal_moves_from(from)) + "}");
                }
            } else if (path == "/apply_move") {
                if (!game.has_value()) {
                    send_json(client, json_error("no game"));
                } else if (!args.contains("from") || !args.contains("to")) {
                    send_json(client, json_error("missing move"));
                } else {
                    const Coordinate from = parse_coordinate(args.at("from"));
                    const Coordinate to = parse_coordinate(args.at("to"));
                    if (!game->apply_move(from, to)) {
                        send_json(client, json_error("illegal move"));
                    } else {
                        send_json(client,
                                  std::string("{\"ok\":true,\"state\":") + game->serialize_state_json(false, false) +
                                      (diplomacy.has_value() ? ",\"diplomacy\":" + diplomacy_snapshot_json(*diplomacy, *game) : "") + "}");
                    }
                }
            } else if (path == "/turn_analysis") {
                if (!game.has_value()) {
                    send_json(client, json_error("no game"));
                } else {
                    const int player_id = args.contains("player") ? std::stoi(args.at("player")) : game->current_player();
                    TurnAdvice advice;
                    advice.player_id = player_id;
                    const bool threat_only = args.contains("mode") && args.at("mode") == "threats";
                    advice.in_check = game->is_in_check(player_id);
                    if (advice.in_check) {
                        advice.king_threats = game->king_threats(player_id);
                    }
                    if (!threat_only) {
                        const std::uint64_t seed = args.contains("seed") ? std::stoull(args.at("seed")) : 0;
                        SimulationRunner runner(seed);
                        const TurnAdvice full_advice = runner.suggest_turn(*game, player_id);
                        advice = full_advice;
                    }
                    send_json(client, std::string("{\"ok\":true,\"turn\":") + serialize_turn_advice_json(advice) + "}");
                }
            } else if (path == "/suggest_ai_turn") {
                if (!game.has_value()) {
                    send_json(client, json_error("no game"));
                } else {
                    const int player_id = args.contains("player") ? std::stoi(args.at("player")) : game->current_player();
                    const std::uint64_t seed = args.contains("seed") ? std::stoull(args.at("seed")) : 0;
                    SimulationRunner runner(seed);
                    const TurnAdvice advice = runner.suggest_turn(*game, player_id);
                    send_json(client, std::string("{\"ok\":true,\"turn\":") + serialize_turn_advice_json(advice) + "}");
                }
            } else if (path == "/apply_ai_turn") {
                if (!game.has_value()) {
                    send_json(client, json_error("no game"));
                } else {
                    const int player_id = args.contains("player") ? std::stoi(args.at("player")) : game->current_player();
                    const bool compact = args.contains("compact") && args.at("compact") == "1";
                    const std::uint64_t seed = args.contains("seed") ? std::stoull(args.at("seed")) : 0;
                    SimulationRunner runner(seed);
                    const TurnAdvice advice = runner.suggest_turn(*game, player_id);
                    std::optional<int> victim;
                    if (advice.has_move) {
                        const auto victim_iterator = game->pieces().find(coord_key(advice.move.to));
                        if (victim_iterator != game->pieces().end() && victim_iterator->second.owner != player_id) {
                            victim = victim_iterator->second.owner;
                        }
                    }
                    if (!advice.has_move || !game->apply_resolved_move(advice.move, true)) {
                        send_json(client, json_error("no legal ai move"));
                    } else {
                        if (diplomacy.has_value()) {
                            update_bridge_diplomacy_after_ai_turn(*diplomacy, *game, player_id, victim);
                        }
                        if (compact) {
                            std::ostringstream payload;
                            payload << "{\"ok\":true,\"compact\":true,\"turn\":" << serialize_turn_advice_json(advice)
                                    << ",\"currentPlayer\":" << game->current_player()
                                    << ",\"gameOver\":" << (game->game_over() ? "true" : "false")
                                    << ",\"winner\":";
                            if (game->winner().has_value()) {
                                payload << *game->winner();
                            } else {
                                payload << "null";
                            }
                            if (diplomacy.has_value() && diplomacy->enabled) {
                                payload << ",\"diplomacy\":" << diplomacy_snapshot_json(*diplomacy, *game);
                            }
                            payload << "}";
                            send_json(client, payload.str());
                        } else {
                            send_json(client,
                                      std::string("{\"ok\":true,\"turn\":") + serialize_turn_advice_json(advice) +
                                          ",\"state\":" + game->serialize_state_json(false, false) +
                                          (diplomacy.has_value() ? ",\"diplomacy\":" + diplomacy_snapshot_json(*diplomacy, *game) : "") + "}");
                        }
                    }
                }
            } else if (path == "/step_ai") {
                // Batch step: compute `count` consecutive AI turns, return as steps array.
                // The JS compact queue consumes one step immediately and caches the rest.
                if (!game.has_value()) {
                    send_json(client, json_error("no game"));
                } else {
                    const int start_player = args.contains("player") ? std::stoi(args.at("player")) : game->current_player();
                    const int count = args.contains("count") ? (std::max)(1, (std::min)(8, std::stoi(args.at("count")))) : 1;
                    const std::uint64_t seed = args.contains("seed") ? std::stoull(args.at("seed")) : 0;
                    SimulationRunner runner(seed);
                    std::ostringstream payload;
                    payload << "{\"ok\":true,\"steps\":[";
                    bool first = true;
                    bool any_moved = false;
                    for (int i = 0; i < count; ++i) {
                        const int pid = game->current_player();
                        if (i == 0 && pid != start_player) break; // desync guard
                        const TurnAdvice advice = runner.suggest_turn(*game, pid);
                        if (!advice.has_move || !game->apply_resolved_move(advice.move, true)) break;
                        if (diplomacy.has_value()) {
                            std::optional<int> victim;
                            const auto vit = game->pieces().find(coord_key(advice.move.to));
                            if (vit != game->pieces().end() && vit->second.owner != pid) victim = vit->second.owner;
                            update_bridge_diplomacy_after_ai_turn(*diplomacy, *game, pid, victim);
                        }
                        if (!first) payload << ",";
                        first = false;
                        payload << "{\"turn\":" << serialize_turn_advice_json(advice)
                                << ",\"currentPlayer\":" << game->current_player()
                                << ",\"gameOver\":" << (game->game_over() ? "true" : "false")
                                << ",\"winner\":";
                        if (game->winner().has_value()) payload << *game->winner(); else payload << "null";
                        payload << "}";
                        any_moved = true;
                        if (game->game_over()) break;
                    }
                    payload << "]}";
                    if (!any_moved) send_json(client, json_error("no legal ai move"));
                    else send_json(client, payload.str());
                }
            } else {
                send_json(client, json_error("unknown path"));
            }
        } catch (const std::exception& exception) {
            send_json(client, json_error(exception.what()));
        }

        closesocket(client);
    }

    closesocket(server);
    WSACleanup();
    return EXIT_SUCCESS;
}

#else
int main() { return 1; }
#endif
