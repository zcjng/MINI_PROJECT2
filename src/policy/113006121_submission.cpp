#include <utility>
#include <algorithm>
#include "113006121_state.hpp"
#include "113006121_submission.hpp"
#include <cstdint>


/*============================================================
 * PVS — eval_ctx
 *
 * Negamax with alpha beta pruning. Caller manages memory.
 *============================================================*/

//Transposition Table
 enum TTFlag {
    TT_EXACT,
    TT_LOWER,
    TT_UPPER
};

struct TTEntry {
    uint64_t key = 0;
    int depth = -1;
    int score = 0;
    TTFlag flag = TT_EXACT;
    Move best_move;
    bool valid = false;
};

static const int TT_SIZE = 1 << 18; 
static TTEntry tt[TT_SIZE];

static TTEntry* tt_probe(uint64_t key){
    TTEntry& e = tt[key & (TT_SIZE - 1)];
    if(e.valid && e.key == key){
        return &e;
    }
    return nullptr;
}

static void tt_store(uint64_t key, int depth, int score, TTFlag flag, const Move& best_move){
    TTEntry& e = tt[key & (TT_SIZE - 1)];

    // A collision must be replaceable even when the unrelated entry happened
    // to be searched more deeply. Otherwise that bucket can become unusable
    // for the rest of the game.
    if(!e.valid || e.key != key || depth >= e.depth){
        e.key = key;
        e.depth = depth;
        e.score = score;
        e.flag = flag;
        e.best_move = best_move;
        e.valid = true;
    }
}

static int score_to_tt(int score, int ply){
    if(score >= P_MAX - 128) return score + ply;
    if(score <= M_MAX + 128) return score - ply;
    return score;
}

static int score_from_tt(int score, int ply){
    if(score >= P_MAX - 128) return score - ply;
    if(score <= M_MAX + 128) return score + ply;
    return score;
}

//------------------------------

static Move killer_moves[64][2];
static bool killer_valid[64][2] = {};
static Move last_best_move;
static bool last_best_valid = false;

static int history_score[BOARD_H][BOARD_W][BOARD_H][BOARD_W] = {};

static bool same_move(const Move& a, const Move& b){
    return a.first == b.first && a.second == b.second;
}


static int piece_value(int p){
    static const int val[7] = {0, 20, 60, 70, 80, 200, 1000};
    if(p < 0 || p > 6) return 0;
    return val[p];
}

static int move_score(State* state, const Move& m, int ply, const Move* tt_move = nullptr){
    int from_r = m.first.first;
    int from_c = m.first.second;
    int to_r = m.second.first;
    int to_c = m.second.second;

    int attacker = state->piece_at(state->player, from_r, from_c);
    int victim = state->piece_at(1 - state->player, to_r, to_c);

    int score = 0;
    if(tt_move && same_move(m, *tt_move)){
        score += 30000000;
    }

    if(ply == 0 && last_best_valid && same_move(m, last_best_move)){
        score += 20000000;
    }

    if(victim){
        score += 100000;
        score += 10 * piece_value(victim);
        score -= piece_value(attacker);

        if(victim == 6){
            score += 10000000;
        }
    } else {
        if(ply < 64){
            if(killer_valid[ply][0] && same_move(m, killer_moves[ply][0]))
                score += 90000;
            else if(killer_valid[ply][1] && same_move(m, killer_moves[ply][1]))
                score += 80000;
        }

        score += history_score[from_r][from_c][to_r][to_c];
    }

    if(attacker == 1 && (to_r == 0 || to_r == state->board_h() - 1)){
        score += piece_value(5);
    }

    return score;
}

static void order_moves(State* state, std::vector<Move>& moves, int ply, const Move* tt_move = nullptr){
    std::sort(moves.begin(), moves.end(),
        [&](const Move& a, const Move& b){
            return move_score(state, a, ply, tt_move) > move_score(state, b, ply, tt_move);
        });
}


static bool is_capture(State* state, const Move& move) {
    int r = move.second.first;
    int c = move.second.second;

    return state->piece_at(1 - state->player, r, c) != 0;
}

static bool is_promotion(State* state, const Move& move) {
    int piece = state->piece_at(
        state->player,
        move.first.first,
        move.first.second
    );
    int target_row = move.second.first;
    return piece == 1 && (target_row == 0 || target_row == BOARD_H - 1);
}

// The game ends by king capture rather than conventional checkmate. Looking
// at the position from the opponent's side tells us whether our king is under
// an immediate capture threat. Quiescence must not accept stand-pat there.
static bool king_is_threatened(State* state) {
    State opponent_view(state->board, 1 - state->player);
    opponent_view.get_legal_actions();
    return opponent_view.game_state == WIN;
}

int PVS::quiescence(
    State *state,
    GameHistory& history,
    int ply,
    int qdepth,
    SearchContext& ctx,
    const PParams& p,
    int alpha,
    int beta
){
    ctx.nodes++;

    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }

    if(ctx.stop){
        return 0;
    }

    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    if(state->game_state == WIN){
        return P_MAX - ply;
    }

    if(state->game_state == DRAW){
        return 0;
    }


    if(qdepth <= 0){
        return state->evaluate(
            p.use_kp_eval,
            p.use_eval_mobility,
            &history
        );
    }

    bool threatened = king_is_threatened(state);

    if(!threatened){
        int stand_pat = state->evaluate(
            p.use_kp_eval,
            p.use_eval_mobility,
            &history
        );

        if(stand_pat >= beta){
            return beta;
        }

        if(stand_pat > alpha){
            alpha = stand_pat;
        }
    }

    std::vector<Move> moves = state->legal_actions;
    order_moves(state, moves, ply);

    for(auto& action : moves){

        // Quiet promotions are tactically forcing and must not disappear at
        // the normal depth horizon. This is especially important in the
        // pawn-race endgames seen against the boss engine.
        if(!threatened && !is_capture(state, action) && !is_promotion(state, action)){
            continue;
        }

        State* next = state->next_state(action);

        bool same = next->same_player_as_parent();

        int child_alpha = same ? alpha : -beta;
        int child_beta  = same ? beta  : -alpha;

        int raw = PVS::quiescence(
            next,
            history,
            ply + 1,
            qdepth - 1,
            ctx,
            p,
            child_alpha,
            child_beta
        );

        int score = same ? raw : -raw;

        delete next;

        if(score >= beta){
            return beta;
        }

        if(score > alpha){
            alpha = score;
        }
    }

    return alpha;
}


int PVS::eval_ctx(
    State *state,
    int depth,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const PParams& p,
    int alpha,
    int beta
){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.stop){
        return 0;
    }


    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    if(state->game_state == WIN){
        return P_MAX - ply; 

    }
    if(state->game_state == DRAW){
        return 0;
    }

    int rep_score;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }

    uint64_t key = state->hash();
    int original_alpha = alpha;
    int original_beta = beta;

    TTEntry* entry = tt_probe(key);
    if(entry && entry->depth >= depth){
        int tt_score = score_from_tt(entry->score, ply);
        if(entry->flag == TT_EXACT){
            return tt_score;
        }
        if(entry->flag == TT_LOWER){
            alpha = std::max(alpha, tt_score);
        }else if(entry->flag == TT_UPPER){
            beta = std::min(beta, tt_score);
        }

        if(alpha >= beta){
            return tt_score;
        }
    }

    history.push(key);

    if(depth <= 0){
        int score = PVS::quiescence(
            state,
            history,
            ply,
            p.quiescence_depth,
            ctx,
            p,
            alpha,
            beta
        );
        history.pop(key);

        return score;
    }

    /* === Negamax loop === */
    int best_score = M_MAX;
    Move best_move;
    bool has_best_move = false;
    

    std::vector<Move> moves = state->legal_actions;

    const Move* tt_move = entry ? &entry->best_move : nullptr;
    order_moves(state, moves, ply, tt_move);


    bool first_child = true;

    int move_index = 0;

    for(auto& action : moves){


        State* next = state->next_state(action);

        bool same = next->same_player_as_parent();

        int score;

        int new_depth = depth - 1;

        bool quiet = !state->piece_at(
            1 - state->player,
            action.second.first,
            action.second.second
        );
        bool promotion = state->piece_at(
            state->player,
            action.first.first,
            action.first.second
        ) == 1 && (action.second.first == 0 || action.second.first == BOARD_H - 1);
        bool can_lmr = p.use_lmr && !first_child && quiet && !promotion
            && depth >= 4 && move_index >= 4;
        if(can_lmr){
            new_depth = depth - 2;
        }

        if(first_child){

            int child_alpha = same ? alpha : -beta;
            int child_beta = same ? beta : -alpha;
    
    
            int raw = PVS::eval_ctx(
                next,
                new_depth,
                history,
                ply + 1,
                ctx,
                p,
                child_alpha,
                child_beta
            );
            score = same ? raw : -raw; 

            first_child = false;

            
        } else {
            // Null-window search
            bool full_window_done = false;
            int child_alpha = same ? alpha : -(alpha + 1);
            int child_beta  = same ? alpha + 1 : -alpha;

            int raw = PVS::eval_ctx(
                next,
                new_depth,
                history,
                ply + 1,
                ctx,
                p,
                child_alpha,
                child_beta
            );

            score = same ? raw : -raw;

            if(can_lmr && score > alpha){
                int child_alpha = same ? alpha : -beta;
                int child_beta  = same ? beta  : -alpha;

                int raw = PVS::eval_ctx(
                    next,
                    depth - 1,
                    history,
                    ply + 1,
                    ctx,
                    p,
                    child_alpha,
                    child_beta
                );

                score = same ? raw : -raw;
                full_window_done = true;
            }

            if(!full_window_done && score > alpha && score < beta){
                child_alpha = same ? alpha : -beta;
                child_beta  = same ? beta  : -alpha;

                raw = PVS::eval_ctx(
                    next,
                    depth - 1,
                    history,
                    ply + 1,
                    ctx,
                    p,
                    child_alpha,
                    child_beta
                );

                score = same ? raw : -raw;
            }
        }

        delete next;


        if(score > best_score){
            best_score = score;
            best_move = action;
            has_best_move = true;
        }

        


        alpha = std::max(alpha, best_score);
        if(alpha >= beta){
            if(!state->piece_at(1 - state->player, action.second.first, action.second.second) && ply < 64){

                if(!killer_valid[ply][0] || !same_move(action, killer_moves[ply][0])){
                    killer_moves[ply][1] = killer_moves[ply][0];
                    killer_valid[ply][1] = killer_valid[ply][0];

                    killer_moves[ply][0] = action;
                    killer_valid[ply][0] = true;
                }

                int fr = action.first.first;
                int fc = action.first.second;
                int tr = action.second.first;
                int tc = action.second.second;

                history_score[fr][fc][tr][tc] = std::min(history_score[fr][fc][tr][tc] + depth * depth, 100000);
            }

            break;
        }
        move_index++;
    }

    if(has_best_move){
        TTFlag flag;

        if(best_score <= original_alpha){
            flag = TT_UPPER;
        }else if(best_score >= original_beta){
            flag = TT_LOWER;
        }else{
            flag = TT_EXACT;
        }

        tt_store(key, depth, score_to_tt(best_score, ply), flag, best_move);
    }
    history.pop(key);
    return best_score;
}


/*============================================================
 * PVS — search
 *
 * Iterate legal moves, call eval_ctx, return SearchResult.
 *============================================================*/
std::vector<Move> extract_pv(State* root, int max_len){
    std::vector<Move> pv;
    State* cur = root;
    bool owns = false;

    for(int i = 0; i < max_len; i++){
        uint64_t key = cur->hash();
        TTEntry* e = tt_probe(key);
        if(!e || !e->valid) break;

        pv.push_back(e->best_move);
        State* next = cur->next_state(e->best_move);
        if(owns) delete cur;
        cur = next;
        owns = true;
    }

    if(owns) delete cur;
    return pv;
}

SearchResult PVS::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    ctx.reset();

    PParams p = PParams::from_map(ctx.params);

    SearchResult result;
    result.depth = depth;
    result.score = M_MAX;

    if(state->legal_actions.empty()){
        state->get_legal_actions();
    }

    if(!state->legal_actions.empty()){
        result.best_move = state->legal_actions[0];
    }

    int alpha = M_MAX;
    int beta = P_MAX;
    int best_score = M_MAX;
    int move_index = 0;
    int total_moves = static_cast<int>(state->legal_actions.size());
    uint64_t root_key = state->hash();
    TTEntry* root_entry = tt_probe(root_key);
    std::vector<Move> moves = state->legal_actions;
    order_moves(state, moves, 0, root_entry ? &root_entry->best_move : nullptr);
    bool first_child = true;

    for(auto& action : moves){
        if(ctx.stop) break;

        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int score;

        if(first_child){
            int child_alpha = same ? alpha : -beta;
            int child_beta  = same ? beta  : -alpha;

            int raw = PVS::eval_ctx(
                next, depth - 1, history, 1, ctx, p,
                child_alpha, child_beta
            );

            score = same ? raw : -raw;
            first_child = false;
        }else{
            int child_alpha = same ? alpha : -(alpha + 1);
            int child_beta  = same ? alpha + 1 : -alpha;

            int raw = PVS::eval_ctx(
                next, depth - 1, history, 1, ctx, p,
                child_alpha, child_beta
            );

            score = same ? raw : -raw;

            if(score > alpha && score < beta){
                child_alpha = same ? alpha : -beta;
                child_beta  = same ? beta  : -alpha;

                raw = PVS::eval_ctx(
                    next, depth - 1, history, 1, ctx, p,
                    child_alpha, child_beta
                );

                score = same ? raw : -raw;
            }
        }

        delete next;

        if(score > best_score){
            best_score = score;
            result.best_move = action;

            if(p.report_partial && ctx.on_root_update){
                ctx.on_root_update({
                    result.best_move, best_score, depth,
                    move_index + 1, total_moves
                });
            }
        }

        alpha = std::max(alpha, best_score);
        move_index++;
    }

    result.score = best_score;
    result.nodes = ctx.nodes;
    result.seldepth = ctx.seldepth;
    if(!moves.empty() && !ctx.stop){
        tt_store(root_key, depth, score_to_tt(best_score, 0), TT_EXACT, result.best_move);
    }
    result.pv = extract_pv(state, depth);
    if(result.pv.empty() && !moves.empty()) result.pv.push_back(result.best_move);
    last_best_move = result.best_move;
    last_best_valid = !moves.empty();
    return result;
}




/*============================================================
 * PVS — default_params / param_defs
 *============================================================*/
ParamMap PVS::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
        {"QuiescenceDepth", "12"},
        {"UseLMR", "true"},
    };
}

std::vector<ParamDef> PVS::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
        {"QuiescenceDepth", ParamDef::SPIN, "12", 1, 32},
        {"UseLMR", ParamDef::CHECK, "true"},
    };
}
