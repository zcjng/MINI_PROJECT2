#include <utility>
#include <algorithm>
#include "state.hpp"
#include "pvs.hpp"


/*============================================================
 * PVS — eval_ctx
 *
 * Negamax with alpha beta pruning. Caller manages memory.
 *============================================================*/

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

static int move_score(State* state, const Move& m, int ply){
    int from_r = m.first.first;
    int from_c = m.first.second;
    int to_r = m.second.first;
    int to_c = m.second.second;

    int attacker = state->piece_at(state->player, from_r, from_c);
    int victim = state->piece_at(1 - state->player, to_r, to_c);

    int score = 0;

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

static void order_moves(State* state, std::vector<Move>& moves, int ply){ //added ply 
    std::sort(moves.begin(), moves.end(),
        [&](const Move& a, const Move& b){
            return move_score(state, a, ply) > move_score(state, b, ply);
        });
}

/*
static bool is_capture(State* state, const Move& move) {
    int r = move.second.first;
    int c = move.second.second;

    return state->piece_at(1 - state->player, r, c) != 0;
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

    if(qdepth <= 0){
        return state->evaluate(
            p.use_kp_eval,
            p.use_eval_mobility,
            &history
        );
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

    std::vector<Move> moves = state->legal_actions;
    order_moves(state, moves, ply);

    for(auto& action : moves){

        if(!is_capture(state, action)){
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
*/

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
    history.push(state->hash());

    if(depth <= 0){
        int score = state->evaluate(
            p.use_kp_eval,
            p.use_eval_mobility,
            &history
        );
        history.pop(state->hash());
        return score;
    }

    /* === Negamax loop === */
    int best_score = M_MAX;
    

    std::vector<Move> moves = state->legal_actions;

    order_moves(state, moves, ply);

    bool first_child = true;

    int move_index = 0;

    for(auto& action : moves){


        State* next = state->next_state(action);

        bool same = next->same_player_as_parent();

        int score;

        int new_depth = depth - 1;

        bool quiet = !state->piece_at(1 - state->player, action.second.first, action.second.second);
        bool can_lmr = !first_child && quiet && depth >= 4 && move_index >= 4;

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
            }
            
            if(score > alpha && score < beta){
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


        if(score > best_score) best_score = score;

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

    history.pop(state->hash());
    return best_score;
}


/*============================================================
 * PVS — search
 *
 * Iterate legal moves, call eval_ctx, return SearchResult.
 *============================================================*/
SearchResult PVS::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    ctx.reset();

    PParams p = PParams::from_map(ctx.params);

    SearchResult final_result;
    final_result.depth = 0;
    final_result.score = M_MAX;
    final_result.nodes = 0;
    final_result.seldepth = 0;

    if(state->legal_actions.empty()){
        state->get_legal_actions();
    }

    if(!state->legal_actions.empty()){
        final_result.best_move = state->legal_actions[0];
    }


    for(int current_depth = 1; current_depth <= depth; current_depth++){

        int alpha = M_MAX;
        int beta = P_MAX;

        SearchResult result;
        result.depth = current_depth;

        int best_score = M_MAX - 10;
        int move_index = 0;
        int total_moves = (int)state->legal_actions.size();

        std::vector<Move> moves = state->legal_actions;
        order_moves(state, moves, 0);

        bool first_child = true;

        for(auto& action : moves){
            if(ctx.stop){
                break;
            }

            State* next = state->next_state(action);
            bool same = next->same_player_as_parent();

            int score;

            if(first_child){
                int child_alpha = same ? alpha : -beta;
                int child_beta  = same ? beta  : -alpha;

                int raw = PVS::eval_ctx(
                    next,
                    current_depth - 1,
                    history,
                    1,
                    ctx,
                    p,
                    child_alpha,
                    child_beta
                );

                score = same ? raw : -raw;
                first_child = false;
            }else{
                int child_alpha = same ? alpha : -(alpha + 1);
                int child_beta  = same ? alpha + 1 : -alpha;

                int raw = PVS::eval_ctx(
                    next,
                    current_depth - 1,
                    history,
                    1,
                    ctx,
                    p,
                    child_alpha,
                    child_beta
                );

                score = same ? raw : -raw;

                if(score > alpha && score < beta){
                    child_alpha = same ? alpha : -beta;
                    child_beta  = same ? beta  : -alpha;

                    raw = PVS::eval_ctx(
                        next,
                        current_depth - 1,
                        history,
                        1,
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
                result.best_move = action;

                if(p.report_partial && ctx.on_root_update){
                    ctx.on_root_update({
                        result.best_move,
                        best_score,
                        current_depth,
                        move_index + 1,
                        total_moves
                    });
                }
            }

            alpha = std::max(alpha, best_score);

            if(alpha >= beta){
                break;
            }

            move_index++;
        }

        if(ctx.stop){
            if(final_result.depth > 0){
                return final_result;
            }
            break;
        }

        result.score = best_score;
        result.depth = current_depth;
        result.nodes = ctx.nodes;
        result.seldepth = ctx.seldepth;
        result.pv = {result.best_move};

        final_result = result;

        last_best_move = result.best_move;
        last_best_valid = true;
    }

    return final_result;
}


/*============================================================
 * PVS — default_params / param_defs
 *============================================================*/
ParamMap PVS::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
    };
}

std::vector<ParamDef> PVS::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
    };
}
