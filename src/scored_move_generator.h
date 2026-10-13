/*
    Wrapper for move_generator used when an ordering plugin is loaded.

    Drains the wrapped generator (plain, PITM, filtering or DB list) into a
    vector, builds mcgs_move_features per move, calls score_move() once per
    move, and yields the moves stable-sorted by (score desc, native index asc).
*/
#pragma once

#include <cstddef>
#include <vector>

#include "game.h"
#include "ordering_plugin_api.h"

class scored_move_generator : public move_generator
{
public:
    // Assumes ownership of gen. g must be the game gen was created for.
    scored_move_generator(move_generator* gen, const game& g, bw to_play,
                          const mcgs_subgame_features& subgame_features,
                          const mcgs_node_context& node_ctx);

    virtual ~scored_move_generator() {}

    void operator++() override;
    operator bool() const override;
    move gen_move() const override;

private:
    std::vector<move> _moves;
    size_t _next_move;
};
