#include "ordering_plugin.h"

#include <dlfcn.h>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "all_game_headers.h"
#include "cgt_basics.h"
#include "database.h"
#include "game.h"
#include "global_database.h"
#include "global_options.h"
#include "impartial_game_wrapper.h"
#include "solver_stats.h"
#include "ThValue.h"

using namespace std;

namespace ordering_plugin {

////////////////////////////////////////////////// plugin state
namespace {

typedef double (*score_subgame_fn)(const mcgs_subgame_features*,
                                   const mcgs_node_context*);
typedef double (*score_move_fn)(const mcgs_subgame_features*,
                                const mcgs_move_features*,
                                const mcgs_node_context*);
typedef int (*int_fn)(void);

struct plugin_state
{
    void* handle = nullptr;
    score_subgame_fn score_subgame = nullptr;
    score_move_fn score_move = nullptr;
    bool needs_t2 = false;
    string path;
};

plugin_state state;

inline double sanitize(double score)
{
    if (std::isnan(score))
        return -numeric_limits<double>::infinity();
    return score;
}

template <class Fn>
Fn lookup_symbol(void* handle, const char* name, bool required)
{
    void* sym = dlsym(handle, name);

    if (sym == nullptr && required)
    {
        const char* err = dlerror();
        throw runtime_error(
            "Error: --ordering-plugin: missing export \"" + string(name) +
            "\"" + (err != nullptr ? string(": ") + err : string()));
    }

    return reinterpret_cast<Fn>(sym);
}

} // namespace

////////////////////////////////////////////////// loading
void load(const string& plugin_path)
{
    if (state.handle != nullptr)
        throw runtime_error("Error: --ordering-plugin given more than once");

    void* handle = dlopen(plugin_path.c_str(), RTLD_NOW | RTLD_LOCAL);

    if (handle == nullptr)
    {
        const char* err = dlerror();
        throw runtime_error("Error: --ordering-plugin: dlopen(\"" +
                            plugin_path + "\") failed" +
                            (err != nullptr ? string(": ") + err : string()));
    }

    plugin_state st;
    st.handle = handle;
    st.path = plugin_path;

    try
    {
        st.score_subgame =
            lookup_symbol<score_subgame_fn>(handle, "score_subgame", true);
        st.score_move = lookup_symbol<score_move_fn>(handle, "score_move", true);

        int_fn version_fn =
            lookup_symbol<int_fn>(handle, "mcgs_ordering_api_version", false);

        if (version_fn != nullptr)
        {
            const int version = version_fn();
            if (version != MCGS_ORDERING_API_VERSION)
                throw runtime_error(
                    "Error: --ordering-plugin: plugin API version " +
                    to_string(version) + " but MCGS expects " +
                    to_string(MCGS_ORDERING_API_VERSION));
        }

        int_fn needs_t2_fn =
            lookup_symbol<int_fn>(handle, "mcgs_ordering_needs_t2", false);

        st.needs_t2 = (needs_t2_fn != nullptr) && (needs_t2_fn() != 0);
    }
    catch (...)
    {
        dlclose(handle);
        throw;
    }

    state = st;
}

void unload()
{
    if (state.handle != nullptr)
        dlclose(state.handle);

    state = plugin_state();
}

bool loaded()
{
    return state.handle != nullptr;
}

bool needs_t2()
{
    return state.needs_t2;
}

const string& path()
{
    return state.path;
}

////////////////////////////////////////////////// scoring
double default_score_subgame(const mcgs_subgame_features& g,
                             const mcgs_node_context& ctx)
{
    if (g.has_temperature)
        return g.temperature;
    return -numeric_limits<double>::infinity();
}

double default_score_move(const mcgs_subgame_features& g,
                          const mcgs_move_features& m,
                          const mcgs_node_context& ctx)
{
    return -static_cast<double>(m.index);
}

double score_subgame(const mcgs_subgame_features& g,
                     const mcgs_node_context& ctx)
{
    if (state.score_subgame != nullptr)
        return sanitize(state.score_subgame(&g, &ctx));
    return sanitize(default_score_subgame(g, ctx));
}

double score_move(const mcgs_subgame_features& g, const mcgs_move_features& m,
                  const mcgs_node_context& ctx)
{
    if (state.score_move != nullptr)
        return sanitize(state.score_move(&g, &m, &ctx));
    return sanitize(default_score_move(g, m, ctx));
}

////////////////////////////////////////////////// feature builders
int32_t game_kind_of(const game& g)
{
    // Built on first use: game_type<T>() values are assigned lazily
    static const vector<pair<game_type_t, int32_t>> TABLE = {
        {game_type<clobber_1xn>(), MCGS_GAME_CLOBBER_1XN},
        {game_type<clobber>(), MCGS_GAME_CLOBBER},
        {game_type<nogo_1xn>(), MCGS_GAME_NOGO_1XN},
        {game_type<nogo>(), MCGS_GAME_NOGO},
        {game_type<domineering>(), MCGS_GAME_DOMINEERING},
        {game_type<elephants>(), MCGS_GAME_ELEPHANTS},
        {game_type<toppling_dominoes>(), MCGS_GAME_TOPPLING_DOMINOES},
        {game_type<cannibal_clobber>(), MCGS_GAME_CANNIBAL_CLOBBER},
        {game_type<amazons>(), MCGS_GAME_AMAZONS},
        {game_type<fission>(), MCGS_GAME_FISSION},
        {game_type<sheep>(), MCGS_GAME_SHEEP},
        {game_type<gen_toads>(), MCGS_GAME_GEN_TOADS},
        {game_type<gen_king_dirt>(), MCGS_GAME_KING_DIRT},
        {game_type<kayles>(), MCGS_GAME_KAYLES},
        {game_type<integer_game>(), MCGS_GAME_INTEGER},
        {game_type<dyadic_rational>(), MCGS_GAME_DYADIC_RATIONAL},
        {game_type<switch_game>(), MCGS_GAME_SWITCH},
        {game_type<up_star>(), MCGS_GAME_UP_STAR},
        {game_type<nimber>(), MCGS_GAME_NIMBER},
    };

    const game_type_t gt = g.game_type();

    for (const pair<game_type_t, int32_t>& entry : TABLE)
        if (entry.first == gt)
            return entry.second;

    return MCGS_GAME_OTHER;
}

int32_t outcome_for_mover(outcome_class oc, bw to_play)
{
    assert(is_black_white(to_play));

    switch (oc)
    {
        case outcome_class::L:
            return (to_play == BLACK) ? MCGS_OUTCOME_WIN : MCGS_OUTCOME_LOSS;
        case outcome_class::R:
            return (to_play == WHITE) ? MCGS_OUTCOME_WIN : MCGS_OUTCOME_LOSS;
        case outcome_class::N:
            return MCGS_OUTCOME_N;
        case outcome_class::P:
            return MCGS_OUTCOME_P;
        case outcome_class::U:
            return MCGS_OUTCOME_UNKNOWN;
    }

    return MCGS_OUTCOME_UNKNOWN;
}

int32_t count_moves(const game& g, bw player)
{
    unique_ptr<move_generator> gen(
        g.create_move_generator(player, MOVE_GENERATOR_TYPE_BASIC));

    int32_t n = 0;
    for (; *gen; ++(*gen))
        n++;

    return n;
}

mcgs_node_context make_node_context(bw to_play, uint64_t depth,
                                    int32_t n_components, int32_t n_numbers,
                                    int32_t total_size, int32_t n_items)
{
    mcgs_node_context ctx;
    ctx.to_play = (to_play == BLACK) ? MCGS_BLACK : MCGS_WHITE;
    ctx.depth = static_cast<int32_t>(depth);
    ctx.n_components = n_components;
    ctx.n_numbers = n_numbers;
    ctx.total_size = total_size;
    ctx.n_items = n_items;
    return ctx;
}

mcgs_subgame_features make_subgame_features(
    const game& g, bw to_play, const optional<ThValue>& temperature,
    const optional<outcome_class>& db_outcome, int32_t n_copies,
    int32_t original_index)
{
    mcgs_subgame_features f;

    f.game_kind = game_kind_of(g);
    f.size = g.complexity_score();
    f.is_number = 0;
    f.in_db = db_outcome.has_value() ? 1 : 0;
    f.has_temperature = temperature.has_value() ? 1 : 0;
    f.temperature = temperature.has_value() ? temperature->ToDouble() : 0.0;
    f.outcome_class = db_outcome.has_value()
                          ? outcome_for_mover(db_outcome.value(), to_play)
                          : MCGS_OUTCOME_UNKNOWN;
    f.n_moves_mover = count_moves(g, to_play);
    f.n_moves_opponent = count_moves(g, opponent(to_play));
    f.n_copies = n_copies;
    f.original_index = original_index;

    return f;
}

mcgs_move_features make_move_features(int32_t index, int32_t n_moves,
                                      bool from_db_list)
{
    mcgs_move_features m;

    m.index = index;
    m.n_moves = n_moves;
    m.rel_pos = (n_moves > 1) ? static_cast<double>(index) / (n_moves - 1)
                              : 0.5;
    m.center_dist = std::fabs(m.rel_pos - 0.5);
    m.from_db_list = from_db_list ? 1 : 0;

    m.t2_valid = 0;
    m.n_pieces = 0;
    m.pieces_total_size = 0;
    m.max_piece_size = 0;
    m.pieces_in_db = 0;
    m.pieces_win = 0;
    m.pieces_loss = 0;
    m.pieces_n = 0;
    m.pieces_p = 0;
    m.pieces_unknown = 0;
    m.has_piece_temperature = 0;
    m.max_piece_temperature = 0.0;
    m.sum_piece_temperature = 0.0;
    m.opp_replies = -1;
    m.mover_replies = -1;

    return m;
}

namespace {

// Accumulate T2 features of one (already normalized) piece
void account_piece(mcgs_move_features& mf, const game& piece, bw to_play,
                   const database* db)
{
    mf.n_pieces++;

    const int32_t size = piece.complexity_score();
    mf.pieces_total_size += size;
    mf.max_piece_size = max(mf.max_piece_size, size);

    mf.opp_replies += count_moves(piece, opponent(to_play));
    mf.mover_replies += count_moves(piece, to_play);

    const db_entry_partisan* entry = nullptr;

    if (db != nullptr && !piece.is_impartial())
        entry = db->get_partisan_ptr(piece);

    if (entry == nullptr)
    {
        mf.pieces_unknown++;
        return;
    }

    mf.pieces_in_db++;

    switch (outcome_for_mover(entry->outcome, to_play))
    {
        case MCGS_OUTCOME_WIN:
            mf.pieces_win++;
            break;
        case MCGS_OUTCOME_LOSS:
            mf.pieces_loss++;
            break;
        case MCGS_OUTCOME_N:
            mf.pieces_n++;
            break;
        case MCGS_OUTCOME_P:
            mf.pieces_p++;
            break;
        default:
            mf.pieces_unknown++;
            break;
    }

    if (entry->thermograph)
    {
        const double temp = entry->thermograph->Temperature().ToDouble();

        if (!mf.has_piece_temperature)
        {
            mf.has_piece_temperature = 1;
            mf.max_piece_temperature = temp;
            mf.sum_piece_temperature = temp;
        }
        else
        {
            mf.max_piece_temperature = max(mf.max_piece_temperature, temp);
            mf.sum_piece_temperature += temp;
        }
    }
}

} // namespace

void fill_t2_features(mcgs_move_features& mf, const game& g_const,
                      const ::move& m, bw to_play)
{
    assert(g_const.is_active());
    assert(is_black_white(to_play));

    // Same cast as sumgame::subgame(): games are mutable inside a const sum
    game* g = const_cast<game*>(&g_const);

    mf.t2_valid = 1;
    mf.opp_replies = 0;
    mf.mover_replies = 0;

    const database* db = global::use_db() ? &get_global_database() : nullptr;

    stats::report_feature_probe();
    g->play(m, to_play);

    split_result sr;
    if (global::play_split())
        sr = g->split();

    if (sr) // g is replaced by its pieces
    {
        for (game* piece : *sr)
        {
            if (global::play_normalize())
                piece->normalize();

            account_piece(mf, *piece, to_play, db);
            delete piece;
        }
    }
    else if (g->has_moves())
    {
        if (global::play_normalize())
        {
            g->normalize();
            account_piece(mf, *g, to_play, db);
            g->undo_normalize();
        }
        else
            account_piece(mf, *g, to_play, db);
    }
    // else: g has no moves left, 0 pieces

    g->undo_move();
}

} // namespace ordering_plugin
