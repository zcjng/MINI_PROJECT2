#include <utility>
#include "state.hpp"
#include "minimax.hpp"


/*============================================================
 * MiniMax — eval_ctx
 *
 * Negamax without pruning. Caller manages memory.
 *============================================================*/
int MiniMax::eval_ctx(
    State *state,
    int depth,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p
){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.stop){
        return 0;
    }

    /* === Lazy move generation (sets game_state) === */
    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    /* === Terminal / leaf checks === */

    // [ Hackathon TODO 3-1 ]
    // return the score for a winning terminal state
    // Hint: prefer faster wins by using ply.
    if(state->game_state == WIN){
        return P_MAX - ply; 
        /* 
            Since this is a terminal win, meaning that the current player can capture the opponent's king,
            the position is considered winning so we use P_MAX to set the score as maximum score, contrary
            to score where it is just material advantage, pst bonuses, and mobilities.
        */
    }
    if(state->game_state == DRAW){
        return 0;
    }

    /* === Repetition check (game-specific) === */
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

    for(auto& action : state->legal_actions){
        // [ Hackathon TODO 3-2 ]
        // create the child state after applying action

        State* next = state->next_state(action);

        bool same = next->same_player_as_parent();

        
        // [Hackathon TODO 3-3]
        // search the child one level deeper

        int raw = MiniMax::eval_ctx(
            next,
            depth - 1,
            history,
            ply + 1,
            ctx,
            p
        );

        int score = same ? raw : -raw; //if the player is the same as parent then no need to negate
            //because the next state is usually represented as the opponent because we switch turns
        // [Hackathon TODO 3-4]
        // convert raw to the current player's perspective.

        delete next;

        // [ Hackathon TODO 3-5 ]
        // update best_score if this child is better.
        if(score > best_score) best_score = score;
    }

    history.pop(state->hash());
    return best_score;
}


/*============================================================
 * MiniMax — search
 *
 * Iterate legal moves, call eval_ctx, return SearchResult.
 *============================================================*/
SearchResult MiniMax::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    ctx.reset();
    MMParams p = MMParams::from_map(ctx.params);
    SearchResult result;
    result.depth = depth;

    if(!state->legal_actions.size()){
        state->get_legal_actions();
    }


    int best_score = M_MAX - 10;
    int move_index = 0;
    int total_moves = (int)state->legal_actions.size();

    for(auto& action : state->legal_actions){
        /* [ Hackathon TODO 4-1 ]
         * search this move like TODO 3, but starting from the root */
        State* next = state->next_state(action);

        bool same = next->same_player_as_parent();

        int raw = MiniMax::eval_ctx(
            next,
            depth - 1,
            history,
            1,      //because we are at the child, ply = 1
            ctx,
            p
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
                                        //meaning beset predicted line of play
        return result;
} 


/*============================================================
 * MiniMax — default_params / param_defs
 *============================================================*/
ParamMap MiniMax::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
    };
}

std::vector<ParamDef> MiniMax::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
    };
}
