#include "scored_move_generator.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

#include "db_move_generator.h"
#include "game.h"
#include "ordering_plugin.h"

scored_move_generator::scored_move_generator(
    move_generator* gen, const game& g, bw to_play,
    const mcgs_subgame_features& subgame_features,
    const mcgs_node_context& node_ctx)
    : move_generator(to_play), _moves(), _next_move(0)
{
    assert(gen != nullptr);

    const bool from_db_list = dynamic_cast<db_move_generator*>(gen) != nullptr;

    std::vector<move> native_moves;
    while (*gen)
    {
        native_moves.push_back(gen->gen_move());
        ++(*gen);
    }

    delete gen;

    const size_t n_moves = native_moves.size();

    mcgs_node_context ctx = node_ctx;
    ctx.n_items = static_cast<int32_t>(n_moves);

    const bool t2 = ordering_plugin::needs_t2();

    std::vector<double> scores(n_moves);
    for (size_t i = 0; i < n_moves; i++)
    {
        mcgs_move_features mf = ordering_plugin::make_move_features(
            static_cast<int32_t>(i), static_cast<int32_t>(n_moves),
            from_db_list);

        if (t2)
            ordering_plugin::fill_t2_features(mf, g, native_moves[i], to_play);

        scores[i] = ordering_plugin::score_move(subgame_features, mf, ctx);
    }

    // Stable sort by score (desc); ties keep the native order
    std::vector<size_t> order(n_moves);
    std::iota(order.begin(), order.end(), 0);

    std::stable_sort(order.begin(), order.end(),
                     [&scores](size_t a, size_t b) -> bool
    {
        return scores[a] > scores[b];
    });

    _moves.reserve(n_moves);
    for (size_t idx : order)
        _moves.push_back(native_moves[idx]);
}

void scored_move_generator::operator++()
{
    assert(*this);
    _next_move++;
}

scored_move_generator::operator bool() const
{
    return _next_move < _moves.size();
}

move scored_move_generator::gen_move() const
{
    assert(*this);
    return _moves[_next_move];
}
