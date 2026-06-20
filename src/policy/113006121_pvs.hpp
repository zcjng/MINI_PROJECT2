#pragma once
#include "search_types.hpp"
#include "game_history.hpp"

struct PParams {
    bool use_kp_eval = true;
    bool use_eval_mobility = true;
    bool report_partial = true;
    int quiescence_depth = 12;
    bool use_lmr = true;

    static PParams from_map(const ParamMap& m){
        PParams p;
        p.use_kp_eval       = param_bool(m, "UseKPEval", true);
        p.use_eval_mobility = param_bool(m, "UseEvalMobility", true);
        p.report_partial    = param_bool(m, "ReportPartial", true);
        p.quiescence_depth  = param_int(m, "QuiescenceDepth", 12);
        p.use_lmr           = param_bool(m, "UseLMR", true);
        return p;
    }
};

class PVS{
public:
    static int eval_ctx(
        State *state,
        int depth,
        GameHistory& history,
        int ply,
        SearchContext& ctx,
        const PParams& p,
        int alpha,
        int beta
    );
    static SearchResult search(
        State *state,
        int depth,
        GameHistory& history,
        SearchContext& ctx
    );

    static ParamMap default_params();
    static std::vector<ParamDef> param_defs();
    static int quiescence(
        State *state,
        GameHistory& history,
        int ply,
        int qdepth,
        SearchContext& ctx,
        const PParams& p,
        int alpha,
        int beta
    );

};
