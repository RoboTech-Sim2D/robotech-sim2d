// -*-c++-*-
// Ported from Cyrus OpenSource move_def/bhv_mark_decision_greedy.cpp
// and move_def/bhv_mark_decisions.cpp (the decision sub-methods).
//
// Key adaptations vs Cyrus:
//   Setting::i()->mDefenseMove->mStartMidMark       → -10.0
//   Setting::i()->mDefenseMove->mMidNear_StartX      → -20.0
//   Strategy::i().tmLine(i) == PostLine::back        → roleNumber 2-5
//   Strategy::i().getTeammatesInPostLine(PostLine::back) → loop 2-5
//   bhv_block::blocker_eval_mark_decision(wm)        → removed (empty vectors)
//   Strategy::i().isDefenseSituation(wm, unum)       → isPersonalDefenseSituation
//   Audio memory                                     → removed
//   midMarkDecision / goalMarkDecision               → self-contained using
//       MarkPositionFinder + BestMatchFinder

#include "bhv_mark_decision_greedy.h"
#include "bhv_basic_block.h"
#include "best_match_finder.h"
#include "mark_position_finder.h"
#include "strategy.h"

#include <rcsc/player/player_agent.h>
#include <rcsc/player/world_model.h>
#include <rcsc/player/intercept_table.h>
#include <rcsc/player/abstract_player_object.h>
#include <rcsc/common/server_param.h>
#include <rcsc/geom/vector_2d.h>
#include <rcsc/geom/angle_deg.h>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>

using namespace rcsc;
using namespace std;

// ── static members ───────────────────────────────────────────────────────────
bool              BhvMarkDecisionGreedy::use_home_pos = false;
pair<long, int>   BhvMarkDecisionGreedy::last_mark    = { 10000, 0 };

// ── constants replacing Setting::i()->mDefenseMove ───────────────────────────
namespace {
    // Ball X threshold: above → MidMark, below → GoalMark
    constexpr double kStartMidMark  = -10.0;
    // Ball X threshold for additional LeadNear marking in mid-mark
    constexpr double kMidNearStartX = -20.0;

    inline bool isBackRole( int unum ) {
        int r = Strategy::i().roleNumber( unum );
        return r >= 2 && r <= 5;
    }
    inline bool isHalfRole( int unum ) {
        int r = Strategy::i().roleNumber( unum );
        return r >= 6 && r <= 8;
    }
    // Build list of defender unums eligible to mark.
    // Excludes the designated blocker so it isn't double-assigned.
    vector<size_t> eligibleDefenders( const WorldModel & wm ) {
        const int blocker = Bhv_BasicBlock::who_is_blocker( wm );
        vector<size_t> tms;
        for ( int t = 2; t <= 11; ++t ) {
            if ( t == blocker ) continue;
            if ( !isBackRole(t) && !isHalfRole(t) ) continue;
            const AbstractPlayerObject * tm = wm.ourPlayer(t);
            if ( !tm || tm->unum() < 1 ) continue;
            tms.push_back( (size_t)t );
        }
        return tms;
    }
    // Write global arrays and extract per-self result
    void applyResults( const WorldModel         & wm,
                       MarkType                   how_mark[12],
                       size_t                     tm_mark_target[12],
                       size_t                     opp_marker[12],
                       MarkType                 & mark_type,
                       int                      & mark_unum,
                       bool                     & blocked,
                       vector<MarkType>         & global_how_mark,
                       vector<size_t>           & global_tm_mark_target,
                       vector<size_t>           & global_opp_marker,
                       size_t                     fastest_opp )
    {
        for ( int t = 1; t <= 11; ++t ) {
            if ( how_mark[t] == MarkType::ThMarkFastestOpp
              || how_mark[t] == MarkType::ThMarkFar )
                how_mark[t] = MarkType::ThMark;
            global_how_mark[t]        = how_mark[t];
            global_tm_mark_target[t]  = tm_mark_target[t];
            global_opp_marker[t]      = opp_marker[t];
        }
        int self = wm.self().unum();
        mark_unum = (int)tm_mark_target[self];
        mark_type = how_mark[self];
        for ( int t = 1; t <= 11; ++t ) {
            if ( fastest_opp == tm_mark_target[t] ) { blocked = true; break; }
        }
    }
}

// ── markDecision ─────────────────────────────────────────────────────────────
MarkDec BhvMarkDecisionGreedy::markDecision( const WorldModel & wm )
{
    int      opp_reach = wm.interceptTable().opponentStep();
    int      tm_reach  = wm.interceptTable().teammateStep();
    Vector2D ball_in   = wm.ball().inertiaPoint( min(opp_reach, tm_reach) );

    // Compute min X among our defensive line players
    double min_our_def_pos_x = 1000.0;
    for ( int t = 2; t <= 11; ++t ) {
        if ( !isBackRole(t) ) continue;
        const AbstractPlayerObject * tm = wm.ourPlayer(t);
        if ( tm && tm->unum() > 0 && tm->pos().x < min_our_def_pos_x )
            min_our_def_pos_x = tm->pos().x;
    }

    double def_line_x = wm.ourDefenseLineX();
    if ( def_line_x < 0.0 && def_line_x < min_our_def_pos_x )
        def_line_x = min_our_def_pos_x;

    def_line_x = min( def_line_x, wm.ball().inertiaPoint(2).x );

    // No marking when ball is far in opponent half and we're not defending
    if ( ball_in.x > 30.0
      && !Strategy::i().isPersonalDefenseSituation( wm, wm.self().unum() ) ) {
        return MarkDec::NoDec;
    }

    return ( ball_in.x > kStartMidMark ) ? MarkDec::MidMark : MarkDec::GoalMark;
}

// ── getOppOffensive ──────────────────────────────────────────────────────────
vector<size_t> BhvMarkDecisionGreedy::getOppOffensive( const WorldModel & wm,
                                                         bool & fastest_opp_marked )
{
    vector<size_t> offensive;
    int      opp_reach   = wm.interceptTable().opponentStep();
    Vector2D ball_in     = wm.ball().inertiaPoint( opp_reach );
    size_t   fastest_opp = ( wm.interceptTable().firstOpponent() == nullptr ? 0
                             : (size_t)wm.interceptTable().firstOpponent()->unum() );

    double offside_margin = 10.0;
    if      ( ball_in.x > 25.0 ) offside_margin = 25.0;
    else if ( ball_in.x >  0.0 ) offside_margin = 20.0;
    else                          offside_margin = 15.0;

    for ( int o = 1; o <= 11; ++o ) {
        const AbstractPlayerObject * opp = wm.theirPlayer(o);
        if ( !opp || opp->unum() < 1 || opp->goalie() ) continue;

        Vector2D opp_pos = opp->pos();

        // Fastest opponent: always include if they're in attacking range
        if ( (size_t)o == fastest_opp ) {
            offensive.push_back( (size_t)o );
            fastest_opp_marked = true;
            continue;
        }

        // Skip opponents far in front of our line
        if ( opp_pos.x > wm.ourDefenseLineX() + offside_margin / 3.0 ) continue;
        if ( opp_pos.x > ball_in.x + 10.0 ) continue;

        offensive.push_back( (size_t)o );
    }
    return offensive;
}

// ── midMarkDecision ──────────────────────────────────────────────────────────
// Assigns ThMark positions (through-pass prevention) to defenders.
// If ball is near our half, also assigns LeadProjectionMark for close threats.
void BhvMarkDecisionGreedy::midMarkDecision(
    PlayerAgent * agent,
    MarkType    & mark_type, int & mark_unum, bool & blocked,
    vector<MarkType>  & global_how_mark,
    vector<size_t>    & global_tm_mark_target,
    vector<size_t>    & global_opp_marker )
{
    const WorldModel & wm = agent->world();
    int      opp_reach   = wm.interceptTable().opponentStep();
    Vector2D ball_in     = wm.ball().inertiaPoint( opp_reach );
    size_t   fastest_opp = ( wm.interceptTable().firstOpponent() == nullptr ? 0
                             : (size_t)wm.interceptTable().firstOpponent()->unum() );

    // Initialise arrays
    double mark_eval[12][12];
    for ( int t = 1; t <= 11; ++t )
        for ( int o = 1; o <= 11; ++o )
            mark_eval[o][t] = 1000.0;

    MarkType how_mark[12]       = {};
    size_t   tm_mark_target[12] = {};
    size_t   opp_marker[12]     = {};
    size_t   opp_mark_count[12] = {};

    bool   fastest_opp_marked = false;
    auto   offensive_opps     = getOppOffensive( wm, fastest_opp_marked );
    auto   tms                = eligibleDefenders( wm );

    // ── ThMark cost matrix ───────────────────────────────────────────────────
    for ( size_t o_idx = 0; o_idx < offensive_opps.size(); ++o_idx ) {
        int o = (int)offensive_opps[o_idx];
        const AbstractPlayerObject * opp = wm.theirPlayer(o);
        double opp_y = opp ? opp->pos().y : 0.0;
        for ( size_t t_idx = 0; t_idx < tms.size(); ++t_idx ) {
            int t = (int)tms[t_idx];
            const AbstractPlayerObject * tm = wm.ourPlayer(t);
            if ( !tm || tm->unum() < 1 ) continue;
            Target target = MarkPositionFinder::getThMarkTarget( t, o, wm );
            double cost = tm->pos().dist( target.pos );
            // Prevent side backs from crossing to wrong band
            int role = Strategy::i().roleNumber(t);
            if ( role == 4 || role == 5 ) {
                Vector2D home = Strategy::i().getPosition(t);
                if ( home.y < -3.0 && opp_y > 5.0 ) cost += 60.0;
                if ( home.y >  3.0 && opp_y < -5.0 ) cost += 60.0;
            }
            mark_eval[o][t] = cost;
        }
    }

    // Sort offensive opponents by proximity to our goal (most dangerous first)
    vector<size_t> sorted_opps = offensive_opps;
    sort( sorted_opps.begin(), sorted_opps.end(), [&]( size_t a, size_t b ) {
        const AbstractPlayerObject * oa = wm.theirPlayer( (int)a );
        const AbstractPlayerObject * ob = wm.theirPlayer( (int)b );
        if ( !oa || !ob ) return false;
        double xa = oa->pos().x, xb = ob->pos().x;
        return xa < xb;
    });

    // Assign: take up to (tms.size()) most dangerous opponents
    size_t n_opps = min( sorted_opps.size(), tms.size() );
    vector<size_t> active_opps( sorted_opps.begin(),
                                 sorted_opps.begin() + (int)n_opps );

    auto result = BestMatchFinder::find_best_dec( mark_eval, tms, active_opps );
    auto & assignments = result.first;

    for ( int i = 0; i < (int)active_opps.size() && i < (int)assignments.size(); ++i ) {
        int t = assignments[i];
        int o = (int)active_opps[i];
        if ( t < 1 || t > 11 ) continue;
        how_mark[t]        = MarkType::ThMark;
        tm_mark_target[t]  = (size_t)o;
        opp_marker[o]      = (size_t)t;
        opp_mark_count[o]++;
    }

    // ── LeadProjectionMark when ball is deep in our half ─────────────────────
    if ( ball_in.x < kMidNearStartX ) {
        double lead_eval[12][12];
        for ( int t = 1; t <= 11; ++t )
            for ( int o = 1; o <= 11; ++o )
                lead_eval[o][t] = 1000.0;

        // Unassigned defenders that could take LeadProjectionMark
        vector<size_t> free_tms;
        for ( auto t : tms ) {
            if ( tm_mark_target[t] == 0 ) free_tms.push_back(t);
        }
        // Unassigned offensive opps
        vector<size_t> unmarked_opps;
        for ( auto o : offensive_opps ) {
            if ( opp_marker[o] == 0 ) unmarked_opps.push_back(o);
        }
        if ( !free_tms.empty() && !unmarked_opps.empty() ) {
            for ( auto o : unmarked_opps ) {
                for ( auto t : free_tms ) {
                    const AbstractPlayerObject * tm = wm.ourPlayer( (int)t );
                    if ( !tm || tm->unum() < 1 ) continue;
                    Target target = MarkPositionFinder::getLeadProjectionMarkTarget( (int)t, (int)o, wm );
                    lead_eval[o][t] = tm->pos().dist( target.pos );
                }
            }
            auto r2 = BestMatchFinder::find_best_dec( lead_eval, free_tms, unmarked_opps );
            auto & a2 = r2.first;
            for ( int i = 0; i < (int)unmarked_opps.size() && i < (int)a2.size(); ++i ) {
                int t = a2[i];
                int o = (int)unmarked_opps[i];
                if ( t < 1 || t > 11 ) continue;
                how_mark[t]       = MarkType::LeadProjectionMark;
                tm_mark_target[t] = (size_t)o;
                opp_marker[o]     = (size_t)t;
            }
        }
    }

    applyResults( wm, how_mark, tm_mark_target, opp_marker,
                  mark_type, mark_unum, blocked,
                  global_how_mark, global_tm_mark_target, global_opp_marker,
                  fastest_opp );
}

// ── goalMarkDecision ─────────────────────────────────────────────────────────
// Ball deep in our half: defenders cover dangerous opponents near our goal.
void BhvMarkDecisionGreedy::goalMarkDecision(
    PlayerAgent * agent,
    MarkType    & mark_type, int & mark_unum, bool & blocked,
    vector<MarkType>  & global_how_mark,
    vector<size_t>    & global_tm_mark_target,
    vector<size_t>    & global_opp_marker )
{
    const WorldModel & wm = agent->world();
    size_t fastest_opp = ( wm.interceptTable().firstOpponent() == nullptr ? 0
                           : (size_t)wm.interceptTable().firstOpponent()->unum() );

    double mark_eval[12][12];
    for ( int t = 1; t <= 11; ++t )
        for ( int o = 1; o <= 11; ++o )
            mark_eval[o][t] = 1000.0;

    MarkType how_mark[12]       = {};
    size_t   tm_mark_target[12] = {};
    size_t   opp_marker[12]     = {};
    size_t   opp_mark_count[12] = {};

    bool   fastest_opp_marked = false;
    auto   offensive_opps     = getOppOffensive( wm, fastest_opp_marked );
    auto   tms                = eligibleDefenders( wm );

    // Build DangerMark cost matrix
    for ( auto o : offensive_opps ) {
        const AbstractPlayerObject * opp = wm.theirPlayer( (int)o );
        double opp_y = opp ? opp->pos().y : 0.0;
        for ( auto t : tms ) {
            const AbstractPlayerObject * tm = wm.ourPlayer( (int)t );
            if ( !tm || tm->unum() < 1 ) continue;
            Target target = MarkPositionFinder::getDengerMarkTarget( (int)t, (int)o, wm );
            double cost = tm->pos().dist( target.pos );
            // Prevent side backs from crossing to wrong band
            int role = Strategy::i().roleNumber( (int)t );
            if ( role == 4 || role == 5 ) {
                Vector2D home = Strategy::i().getPosition( (int)t );
                if ( home.y < -3.0 && opp_y > 5.0 ) cost += 60.0;
                if ( home.y >  3.0 && opp_y < -5.0 ) cost += 60.0;
            }
            mark_eval[o][t] = cost;
        }
    }

    // Sort by most dangerous (closest to our goal)
    vector<size_t> sorted_opps = offensive_opps;
    sort( sorted_opps.begin(), sorted_opps.end(), [&]( size_t a, size_t b ) {
        const AbstractPlayerObject * oa = wm.theirPlayer( (int)a );
        const AbstractPlayerObject * ob = wm.theirPlayer( (int)b );
        if ( !oa || !ob ) return false;
        return oa->pos().x < ob->pos().x;
    });

    size_t n_opps = min( sorted_opps.size(), tms.size() );
    vector<size_t> active_opps( sorted_opps.begin(),
                                 sorted_opps.begin() + (int)n_opps );

    auto result      = BestMatchFinder::find_best_dec( mark_eval, tms, active_opps );
    auto & assignments = result.first;

    for ( int i = 0; i < (int)active_opps.size() && i < (int)assignments.size(); ++i ) {
        int t = assignments[i];
        int o = (int)active_opps[i];
        if ( t < 1 || t > 11 ) continue;
        how_mark[t]       = MarkType::DangerMark;
        tm_mark_target[t] = (size_t)o;
        opp_marker[o]     = (size_t)t;
        opp_mark_count[o]++;
    }

    applyResults( wm, how_mark, tm_mark_target, opp_marker,
                  mark_type, mark_unum, blocked,
                  global_how_mark, global_tm_mark_target, global_opp_marker,
                  fastest_opp );
}

// ── antiDefMarkDecision ──────────────────────────────────────────────────────
// Ball in opponent half with no personal defense situation: no marking needed.
void BhvMarkDecisionGreedy::antiDefMarkDecision(
    const WorldModel & wm,
    MarkType & mark_type, int & mark_unum, bool & /*blocked*/,
    vector<MarkType>  & global_how_mark,
    vector<size_t>    & global_tm_mark_target,
    vector<size_t>    & global_opp_marker )
{
    mark_type  = MarkType::NoType;
    mark_unum  = 0;
    for ( int t = 1; t <= 11; ++t ) {
        global_how_mark[t]       = MarkType::NoType;
        global_tm_mark_target[t] = 0;
        global_opp_marker[t]     = 0;
    }
}

// ── getMarkTargets ───────────────────────────────────────────────────────────
void BhvMarkDecisionGreedy::getMarkTargets(
    PlayerAgent * agent,
    MarkType    & mark_type, int & mark_unum, bool & blocked,
    vector<MarkType>  & global_how_mark,
    vector<size_t>    & global_tm_mark_target,
    vector<size_t>    & global_opp_marker )
{
    const WorldModel & wm = agent->world();
    use_home_pos = false;

    // Ensure vectors are big enough (index 0..11 used)
    global_how_mark.assign( 12, MarkType::NoType );
    global_tm_mark_target.assign( 12, 0 );
    global_opp_marker.assign( 12, 0 );

    if ( wm.interceptTable().firstOpponent() == nullptr
      || wm.interceptTable().firstOpponent()->unum() < 1 ) {
        mark_type = MarkType::NoType;
        mark_unum = 0;
        return;
    }

    switch ( markDecision(wm) ) {
    case MarkDec::AntiDef:
        antiDefMarkDecision( wm, mark_type, mark_unum, blocked,
                             global_how_mark, global_tm_mark_target, global_opp_marker );
        break;
    case MarkDec::MidMark:
        midMarkDecision( agent, mark_type, mark_unum, blocked,
                         global_how_mark, global_tm_mark_target, global_opp_marker );
        break;
    case MarkDec::GoalMark:
        goalMarkDecision( agent, mark_type, mark_unum, blocked,
                          global_how_mark, global_tm_mark_target, global_opp_marker );
        break;
    case MarkDec::JustBlock:
    case MarkDec::NoDec:
    default:
        mark_type = MarkType::NoType;
        mark_unum = 0;
        break;
    }
}
