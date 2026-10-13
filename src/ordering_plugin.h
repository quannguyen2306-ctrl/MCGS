/*
    Move-ordering plugin host.

    Loads an evolved ordering plugin (shared library exporting the C ABI in
    ordering_plugin_api.h, master copy in evolve/plugin_api), builds the
    feature structs the plugin sees, and scores components/moves. When no
    plugin is loaded, score_subgame()/score_move() use a built-in default
    that reproduces MCGS's native order (hottest component first, moves in
    native order); the search itself never calls them in that case.

    See sumgame_move_generator (component order) and scored_move_generator
    (move order) for the hook points.
*/
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "ordering_plugin_api.h"
#include "cgt_basics.h"
#include "game.h"
#include "ThValue.h"

namespace ordering_plugin {

////////////////////////////////////////////////// loading
// Throws std::runtime_error with a readable message on failure.
void load(const std::string& path);
void unload();

bool loaded();
bool needs_t2();
const std::string& path();

////////////////////////////////////////////////// scoring
// Dispatch to the plugin if loaded, else to the built-in default. NaN -> -inf.
double score_subgame(const mcgs_subgame_features& g,
                     const mcgs_node_context& ctx);
double score_move(const mcgs_subgame_features& g, const mcgs_move_features& m,
                  const mcgs_node_context& ctx);

// Built-in default scorer (same as evolve/seeds/mcgs_default.cpp).
double default_score_subgame(const mcgs_subgame_features& g,
                             const mcgs_node_context& ctx);
double default_score_move(const mcgs_subgame_features& g,
                          const mcgs_move_features& m,
                          const mcgs_node_context& ctx);

////////////////////////////////////////////////// feature builders
// MCGS runtime game type -> mcgs_game_kind (MCGS_GAME_OTHER if unknown)
int32_t game_kind_of(const game& g);

// outcome_class (L/R/N/P/U) -> mcgs_outcome from the mover's point of view
int32_t outcome_for_mover(outcome_class oc, bw to_play);

// Legal moves of `player` in g, before any DB dominated-move filtering
int32_t count_moves(const game& g, bw player);

mcgs_node_context make_node_context(bw to_play, uint64_t depth,
                                    int32_t n_components, int32_t n_numbers,
                                    int32_t total_size, int32_t n_items);

mcgs_subgame_features make_subgame_features(
    const game& g, bw to_play, const std::optional<ThValue>& temperature,
    const std::optional<outcome_class>& db_outcome, int32_t n_copies,
    int32_t original_index);

// T1 only; t2_valid == 0
mcgs_move_features make_move_features(int32_t index, int32_t n_moves,
                                      bool from_db_list);

/*
    T2: play m on g, split, normalize the pieces, look them up in the DB,
    count replies, then undo everything. g is left exactly as it was
    (same sequence as sumgame::play_sum / undo_move). Requires g active.
*/
void fill_t2_features(mcgs_move_features& mf, const game& g, const move& m,
                      bw to_play);

} // namespace ordering_plugin
