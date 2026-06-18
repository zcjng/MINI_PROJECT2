#include <utility>
#include <algorithm>
#include "state.hpp"
#include "abmove.hpp"


/*============================================================
 * ABMove — eval_ctx
 *
 * Negamax with alpha beta pruning. Caller manages memory.
 *============================================================*/

static void order_moves(State* state, std::vector<Move>& moves){
    std::sort(moves.begin(), moves.end(),
        [&](const Move& a, const Move& b){

            int ar = a.second.first;
            int ac = a.second.second;

            int br = b.second.first;
            int bc = b.second.second;

            int capA = state->piece_at(1-state->player, ar, ac);
            int capB = state->piece_at(1-state->player, br, bc);

            return capA > capB;
        });
}

int ABMove::eval_ctx(
    State *state,
    int depth,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const ABMParams& p,
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
            p.use_kp_eval, p.use_eval_mobility, &history
        ); 
        history.pop(state->hash());
        return score;
    }

    /* === Negamax loop === */
    int best_score = M_MAX;
    

    std::vector<Move> moves = state->legal_actions;

    order_moves(state, moves);


    for(auto& action : moves){


        State* next = state->next_state(action);

        bool same = next->same_player_as_parent();

        
        int child_alpha = same ? alpha : -beta;
        int child_beta = same ? beta : -alpha;


        int raw = ABMove::eval_ctx(
            next,
            depth - 1,
            history,
            ply + 1,
            ctx,
            p,
            child_alpha,
            child_beta
        );

        int score = same ? raw : -raw; 


        delete next;


        if(score > best_score) best_score = score;

        alpha = std::max(alpha, best_score);
        if(alpha >= beta) break;

    }

    history.pop(state->hash());
    return best_score;
}


/*============================================================
 * ABMove — search
 *
 * Iterate legal moves, call eval_ctx, return SearchResult.
 *============================================================*/
SearchResult ABMove::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    int alpha = M_MAX;
    int beta = P_MAX;
    ctx.reset();
    ABMParams p = ABMParams::from_map(ctx.params);
    SearchResult result;
    result.depth = depth;

    if(!state->legal_actions.size()){
        state->get_legal_actions();
    }


    int best_score = M_MAX - 10;
    int move_index = 0;
    int total_moves = (int)state->legal_actions.size();


    std::vector<Move> moves = state->legal_actions;

    order_moves(state, moves);

    for(auto& action : moves){
        /* [ Hackathon TODO 4-1 ]
         * search this move like TODO 3, but starting from the root */
        State* next = state->next_state(action);

        bool same = next->same_player_as_parent();

        int child_alpha = same ? alpha : -beta;
        int child_beta = same ? beta : -alpha;

        int raw = ABMove::eval_ctx(
            next,
            depth - 1,
            history,
            1,      //because we are at the child, ply = 1
            ctx,
            p,
            child_alpha,
            child_beta
        );
        

        int score = same ? raw : -raw;

        delete next;


        if(score > best_score){
            // [ Hackathon TODO 4-2 ]
            // keep this move if it is the best so far
            best_score = score;
            result.best_move = action;
            //report partial -> someone is listening for progress updates
            // so the engine sends updates while it is thinking
            
            if(p.report_partial && ctx.on_root_update){ //live search progress
                ctx.on_root_update({result.best_move, best_score, depth, move_index + 1, total_moves});
            }
            //basically when you press analyze on the gui

            //on_root_update, means someone has provided callback function to receive the progress updates
        }   // ON_ROOT_UPDATE -> Best move so far, best score so far, current search depth, which legal move index
                //total number of root moves

        alpha = std::max(alpha, best_score);
        if(alpha >= beta) break;

        move_index++;
        // move to the next root move
        // checking move 1 of 7 
        // checking move 2 of 7 
        // ...
    }

    // [ Hackathon TODO 4-3 ]
    // update result and return
        result.score = best_score;
        result.depth = depth;
        result.nodes = ctx.nodes;
        result.seldepth = ctx.seldepth;
        result.pv = {result.best_move}; //principal variation
                                        //meaning best predicted line of play
        return result;
} 


/*============================================================
 * ABMove — default_params / param_defs
 *============================================================*/
ParamMap ABMove::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
    };
}

std::vector<ParamDef> ABMove::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
    };
}
