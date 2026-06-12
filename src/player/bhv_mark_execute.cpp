// -*-c++-*-
// Ported from Cyrus OpenSource move_def/bhv_mark_execute.cpp (Phase 4)
//
// Adaptations vs Cyrus:
//   bhv_block::*              → removed (Phase 5); blocker=0, Block type skipped
//   bhv_mark_intention.h      → removed; no IntentionMark multi-cycle state
//   Bhv_DefensiveMove::setDefNeckWithBall → replaced with simple neck
//   Strategy::i().tmLine()    → roleNumber() range checks
//   Strategy::getNormalDashPower → Strategy::get_normal_dash_power
//   Setting::i()->mDefenseMove->mFixThMarkY   → true
//   Setting::i()->mDefenseMove->mGoToDefendX  → true
//   Setting::i()->mDefenseMove->mStartMidMark+10 → 0.0
//   Setting::i()->mDefenseMove->mBackBlockMaxXToDefHPosX → 10.0
//   Body_GoToPoint param order fix for lead_mark_move

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "bhv_mark_execute.h"
#include "bhv_mark_decision_greedy.h"
#include "bhv_basic_block.h"
#include "mark_position_finder.h"
#include "strategy.h"
#include "bhv_basic_tackle.h"

#include "basic_actions/body_go_to_point.h"
#include "basic_actions/body_turn_to_point.h"
#include "basic_actions/body_turn_to_angle.h"
#include "basic_actions/body_turn_to_ball.h"
#include "basic_actions/neck_turn_to_ball.h"
#include "basic_actions/neck_turn_to_ball_or_scan.h"
#include "setplay/bhv_set_play.h"

#include <rcsc/player/player_agent.h>
#include <rcsc/player/world_model.h>
#include <rcsc/player/intercept_table.h>
#include <rcsc/player/abstract_player_object.h>
#include <rcsc/player/debug_client.h>
#include <rcsc/common/server_param.h>
#include <rcsc/common/logger.h>
#include <rcsc/geom/vector_2d.h>
#include <rcsc/geom/angle_deg.h>
#include <rcsc/geom/line_2d.h>
#include <rcsc/geom/segment_2d.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace rcsc;
using namespace std;

namespace {
    inline bool isBackRole( const WorldModel & wm, int unum ) {
        int r = Strategy::i().roleNumber( unum );
        return r >= 2 && r <= 5;
    }
    inline bool isHalfRole( const WorldModel & wm, int unum ) {
        int r = Strategy::i().roleNumber( unum );
        return r >= 6 && r <= 8;
    }
    inline bool isForwardRole( const WorldModel & wm, int unum ) {
        int r = Strategy::i().roleNumber( unum );
        return r >= 9 && r <= 11;
    }

    void setNeckAfterMark( PlayerAgent * agent ) {
        const WorldModel & wm = agent->world();
        if ( wm.kickableOpponent() && wm.ball().distFromSelf() < 18.0 )
            agent->setNeckAction( new Neck_TurnToBall() );
        else
            agent->setNeckAction( new Neck_TurnToBallOrScan( 0 ) );
    }
}

// ── execute ──────────────────────────────────────────────────────────────────
bool bhv_mark_execute::execute( PlayerAgent * agent )
{
    const WorldModel & wm = agent->world();

    if ( do_tackle( agent ) ) return true;

    if ( wm.interceptTable().firstOpponent() == nullptr
      || wm.interceptTable().firstOpponent()->unum() < 1 )
        return false;

    // Phase 5: try block before marking — Bhv_BasicBlock internally selects
    // the best blocker; returns false for all other players.
    if ( Bhv_BasicBlock().execute( agent ) ) {
        agent->debugClient().addMessage( "Block" );
        return true;
    }

    int      mark_unum   = 0;
    bool     blocked     = false;
    int      opp_cycle   = wm.interceptTable().opponentStep();
    Vector2D ball_in     = wm.ball().inertiaPoint( opp_cycle );
    MarkType mark_type   = MarkType::NoType;

    vector<MarkType>   global_how_mark( 12, MarkType::NoType );
    vector<size_t>     global_tm_mark_target( 12, 0 );
    vector<size_t>     global_opp_marker( 12, 0 );

    BhvMarkDecisionGreedy().getMarkTargets( agent, mark_type, mark_unum, blocked,
                                            global_how_mark,
                                            global_tm_mark_target,
                                            global_opp_marker );

    if ( mark_unum > 0 || mark_type == MarkType::Goal_keep ) {
        if ( run_mark( agent, mark_unum, mark_type ) ) {
            agent->debugClient().addMessage( "RunMark" );
            return true;
        }
    } else {
        // No assignment for self: fall back to defense positioning
        if ( defenseBeInBack( agent ) ) return true;
    }

    // mGoToDefendX = true: unassigned backs slide to average ThMark x
    if ( isBackRole( wm, wm.self().unum() ) ) {
        vector<double> th_mark_xs;
        for ( int t = 1; t <= 11; ++t ) {
            if ( t == wm.self().unum() ) continue;
            if ( !isBackRole( wm, t ) ) continue;
            if ( global_how_mark[t] == MarkType::ThMark
              && global_tm_mark_target[t] > 0 ) {
                Target tgt = MarkPositionFinder::getThMarkTarget(
                    t, (int)global_tm_mark_target[t], wm );
                th_mark_xs.push_back( tgt.pos.x );
            }
        }
        if ( !th_mark_xs.empty() ) {
            double sum = 0.0;
            for ( double x : th_mark_xs ) sum += x;
            double avg_x = sum / (double)th_mark_xs.size();

            Vector2D target = Strategy::i().getPosition( wm.self().unum() );
            target.x = avg_x;

            agent->debugClient().setTarget( target );
            agent->debugClient().addMessage( "DefendLine" );

            double dist_thr  = max( 1.0, wm.ball().distFromSelf() * 0.1 );
            double dash_power = Strategy::get_normal_dash_power( wm );

            if ( !Body_GoToPoint( target, dist_thr, dash_power ).execute( agent ) )
                Body_TurnToBall().execute( agent );

            setNeckAfterMark( agent );
            return true;
        }
    }

    return false;
}

// ── run_mark ─────────────────────────────────────────────────────────────────
bool bhv_mark_execute::run_mark( PlayerAgent * agent, int mark_unum, MarkType marktype )
{
    const WorldModel & wm = agent->world();

    if ( wm.theirPlayer( mark_unum ) == nullptr
      || wm.theirPlayer( mark_unum )->unum() < 1 )
        return false;

    const AbstractPlayerObject * opp = wm.theirPlayer( mark_unum );

    // Block: let Bhv_BasicBlock handle it (already tried in execute() before run_mark)
    if ( marktype == MarkType::Block ) return Bhv_BasicBlock().execute( agent );

    double   dist_thr = 1.0;
    Target   target;
    target.pos = Strategy::i().getPosition( wm.self().unum() );

    set_mark_target_thr( wm, opp, marktype, target, dist_thr );

    // Adjust for set-play distance rule
    if ( wm.gameMode().type() != GameMode::PlayOn )
        target.pos = change_position_set_play( wm, target.pos );

    agent->debugClient().addCircle( target.pos, 1.0 );
    agent->debugClient().addMessage( "Mark%d", mark_unum );

    do_move_mark( agent, target, dist_thr, marktype, mark_unum );

    setNeckAfterMark( agent );
    return true;
}

// ── set_mark_target_thr ──────────────────────────────────────────────────────
void bhv_mark_execute::set_mark_target_thr( const WorldModel          & wm,
                                             const AbstractPlayerObject * opp,
                                             MarkType                    mark_type,
                                             Target                    & target,
                                             double                    & dist_thr )
{
    int      self_unum = wm.self().unum();
    int      opp_cycle = wm.interceptTable().opponentStep();
    Vector2D ball_in   = wm.ball().inertiaPoint( opp_cycle );

    switch ( mark_type ) {
    case MarkType::LeadProjectionMark: {
        target = MarkPositionFinder::getLeadProjectionMarkTarget( self_unum, opp->unum(), wm );
        double z = max( 1.0, ball_in.dist( target.pos ) * 0.1 );
        dist_thr = z;
        if ( ball_in.dist( target.pos ) < 30.0 && opp->vel().r() > 0.1 )
            dist_thr = 0.5;
        break;
    }
    case MarkType::LeadNearMark: {
        target = MarkPositionFinder::getLeadNearMarkTarget( self_unum, opp->unum(), wm );
        double z = max( 1.0, ball_in.dist( target.pos ) * 0.1 );
        dist_thr = 0.5 * z;
        break;
    }
    case MarkType::ThMark:
    case MarkType::ThMarkFastestOpp:
    case MarkType::ThMarkFar: {
        target = MarkPositionFinder::getThMarkTarget( self_unum, opp->unum(), wm );
        double z = max( 1.0, ball_in.dist( target.pos ) * 0.1 );
        dist_thr = 0.5 * z;
        if ( ball_in.x > 25.0 && dist_thr < 2.0 ) dist_thr = 2.0;
        break;
    }
    case MarkType::DangerMark: {
        target = MarkPositionFinder::getDengerMarkTarget( self_unum, opp->unum(), wm );
        double z = max( 1.0, ball_in.dist( target.pos ) * 0.1 );
        dist_thr = 0.3 * z;
        if ( wm.gameMode().type() != GameMode::PlayOn ) dist_thr *= 1.5;
        break;
    }
    default:
        break;
    }

    if ( wm.gameMode().type() != GameMode::PlayOn )
        dist_thr = max( 1.5, dist_thr );

    // Clamp Y for wide roles (side backs 4-5, side halves 7-8) to prevent
    // extreme lateral opening that leaves central gaps.
    int role = Strategy::i().roleNumber( self_unum );
    if ( role == 4 || role == 5 || role == 7 || role == 8 ) {
        const double max_y = 21.0;
        target.pos.y = std::max( -max_y, std::min( max_y, target.pos.y ) );
    }
}

// ── do_move_mark ─────────────────────────────────────────────────────────────
bool bhv_mark_execute::do_move_mark( PlayerAgent * agent, Target targ,
                                      double dist_thr, MarkType marktype,
                                      int opp_unum )
{
    const WorldModel & wm       = agent->world();
    Vector2D           self_pos  = wm.self().pos();
    Vector2D           target_pos = targ.pos;
    Vector2D           opp_pos   = wm.theirPlayer( opp_unum )->pos();

    // Turn to face mark angle when already close (non-ThMark only)
    if ( marktype != MarkType::ThMark ) {
        if ( self_pos.dist( target_pos ) < dist_thr
          && targ.th.degree() != 1000.0 ) {
            if ( Body_TurnToAngle( targ.th ).execute( agent ) )
                return true;
        }
    }

    if ( marktype == MarkType::ThMark
      || marktype == MarkType::ThMarkFastestOpp
      || marktype == MarkType::ThMarkFar ) {
        double dp = th_mark_power( agent, opp_pos, target_pos );
        th_mark_move( agent, targ, dp, dist_thr, opp_unum );
    } else if ( marktype == MarkType::LeadProjectionMark
             || marktype == MarkType::LeadNearMark ) {
        double dp = lead_mark_power( agent, opp_pos, target_pos );
        lead_mark_move( agent, targ, dp, dist_thr, marktype, opp_pos );
    } else {
        double dp = other_mark_power( agent, opp_pos, target_pos );
        other_mark_move( agent, targ, dp, dist_thr );
    }
    return true;
}

// ── th_mark_power ─────────────────────────────────────────────────────────────
double bhv_mark_execute::th_mark_power( PlayerAgent * agent,
                                         Vector2D opp_pos, Vector2D target_pos )
{
    const WorldModel & wm       = agent->world();
    Vector2D           self_pos  = wm.self().pos();
    int                opp_min   = wm.interceptTable().opponentStep();
    Vector2D           ball_in   = wm.ball().inertiaPoint( opp_min );
    double             dash_power = Strategy::get_normal_dash_power( wm );

    double z = 1.0;
    if ( wm.self().stamina() < 3500.0 ) z = 0.5;
    else if ( wm.self().stamina() < 4500.0 ) z = 0.7;

    if ( fabs( target_pos.x - self_pos.x ) > 5.0 * z ) dash_power = 100.0;
    if ( fabs( target_pos.y - self_pos.y ) > 5.0 * z ) dash_power = 100.0;
    if ( ball_in.dist(opp_pos) < 20.0*z && target_pos.dist(self_pos) > 2.0
      && opp_pos.x - target_pos.x < 5.0 && target_pos.x < opp_pos.x )
        dash_power = 100.0;
    if ( opp_pos.x < target_pos.x && self_pos.x < target_pos.x - 2.0 )
        dash_power = 100.0;
    if ( ball_in.dist(opp_pos) < 20.0*z && opp_min <= 2
      && target_pos.x < opp_pos.x && opp_pos.x < target_pos.x + 7.0*z )
        dash_power = 100.0;
    if ( wm.self().stamina() < 3000.0 )
        dash_power = Strategy::get_normal_dash_power( wm );

    return dash_power;
}

// ── th_mark_move ──────────────────────────────────────────────────────────────
void bhv_mark_execute::th_mark_move( PlayerAgent * agent, Target targ,
                                      double dash_power, double dist_thr,
                                      int opp_unum )
{
    const WorldModel & wm       = agent->world();
    Vector2D           self_pos  = wm.self().pos();
    Vector2D           target_pos = targ.pos;
    Vector2D           self_hpos = Strategy::i().getPosition( wm.self().unum() );
    Vector2D           opp_pos   = wm.theirPlayer( opp_unum )->pos();
    Vector2D           ball_pos  = wm.ball().inertiaPoint( wm.interceptTable().opponentStep() );
    int                opp_min   = wm.interceptTable().opponentStep();

    // mFixThMarkY = true: keep lateral home position for backs
    if ( isBackRole( wm, wm.self().unum() ) ) {
        if ( fabs( self_hpos.y - target_pos.y ) > 5.0
          && ( ball_pos - opp_pos ).th().abs() > 30.0 ) {
            target_pos.y = self_hpos.y;
            targ.pos.y   = self_hpos.y;
        }
    }

    if ( self_pos.dist( target_pos ) < dist_thr && targ.th.degree() != 1000.0 ) {
        if ( Body_TurnToAngle( targ.th ).execute( agent ) ) return;
    }

    if ( self_pos.dist( target_pos ) < 1.0 ) {
        double body_dif = ( targ.th - wm.self().body() ).abs();
        if ( body_dif < 20.0 && self_pos.dist( target_pos ) < dist_thr / 2.0 ) {
            agent->doDash( dash_power,
                           ( target_pos - ( self_pos + wm.self().vel() ) ).th()
                           - wm.self().body() );
            return;
        }
    } else if ( self_pos.dist( target_pos ) < dist_thr + 2.0 && opp_min > 3 ) {
        double body_dif = ( targ.th - wm.self().body() ).abs();
        if ( body_dif < 20.0 ) {
            agent->doDash( dash_power,
                           ( target_pos - ( self_pos + wm.self().vel() ) ).th()
                           - wm.self().body() );
            return;
        }
    }

    Body_GoToPoint( target_pos, dist_thr, dash_power,
                    1.3, 1, false, 15.0 ).execute( agent );
}

// ── lead_mark_power ───────────────────────────────────────────────────────────
double bhv_mark_execute::lead_mark_power( PlayerAgent * agent,
                                           Vector2D opp_pos, Vector2D target_pos )
{
    const WorldModel & wm       = agent->world();
    Vector2D           self_pos  = wm.self().pos();
    int                opp_min   = wm.interceptTable().opponentStep();
    Vector2D           ball_in   = wm.ball().inertiaPoint( opp_min );
    double             dash_power = Strategy::get_normal_dash_power( wm );

    bool is_back    = isBackRole( wm, wm.self().unum() );
    bool is_forward = isForwardRole( wm, wm.self().unum() );

    if ( opp_min < 3
      && !( ball_in.dist(target_pos) > 15.0 && ball_in.x > -25.0 && !is_back ) )
        dash_power = 100.0;
    if ( ball_in.x < wm.ourDefenseLineX() - 10.0 ) dash_power = 100.0;
    if ( target_pos.dist( Vector2D(-52.0,0.0) ) < 25.0 )
        if ( is_back || target_pos.dist(ball_in) < 20.0 ) dash_power = 100.0;
    if ( wm.interceptTable().firstOpponent()
      && wm.interceptTable().firstOpponent()->pos().dist(target_pos) < 5.0 )
        dash_power = 100.0;
    if ( target_pos.x < -35.0 ) dash_power = 100.0;
    if ( opp_pos.dist(ball_in) > 40.0 && self_pos.dist(target_pos) < 10.0 )
        dash_power = Strategy::get_normal_dash_power( wm );
    if ( opp_pos.dist(ball_in) > 30.0 && self_pos.dist(target_pos) < 5.0 )
        dash_power = Strategy::get_normal_dash_power( wm );
    if ( opp_pos.dist(ball_in) > 20.0 && self_pos.dist(target_pos) < 3.0 )
        dash_power = Strategy::get_normal_dash_power( wm );
    if ( wm.self().stamina() < 4500.0 )
        dash_power = Strategy::get_normal_dash_power( wm );
    if ( wm.self().stamina() < 5500.0 && is_forward )
        dash_power = Strategy::get_normal_dash_power( wm );

    return dash_power;
}

// ── lead_mark_move ────────────────────────────────────────────────────────────
void bhv_mark_execute::lead_mark_move( PlayerAgent * agent, Target targ,
                                        double dash_power, double dist_thr,
                                        MarkType mark_type, Vector2D opp_pos )
{
    const WorldModel & wm       = agent->world();
    int                opp_min   = wm.interceptTable().opponentStep();
    Vector2D           ball_in   = wm.ball().inertiaPoint( opp_min );
    Vector2D           self_pos  = wm.self().pos();
    Vector2D           target_pos = targ.pos;

    // Near-goal direct dash
    if ( ( self_pos.dist( Vector2D(-52.0, 0.0) ) < 25.0
        || ( self_pos.dist(target_pos) < 2.0 && ball_in.x < -30.0 )
        || ( fabs(target_pos.x - wm.ourDefenseLineX()) < 5.0 && target_pos.x < -35.0 ) )
      && wm.self().stamina() > 3000.0 ) {
        AngleDeg dir = ( target_pos - self_pos ).th() - wm.self().body();
        if ( target_pos.dist(self_pos) < 1.0
          || ( fabs(dir.degree()) < 10.0 && fabs(dir.degree()) > 170.0 ) ) {
            if ( agent->doDash( 100.0, dir ) ) return;
        }
    }

    // LeadNearMark: adjust target to pass-line projection
    if ( mark_type == MarkType::LeadNearMark ) {
        Line2D ball_opp_line( ball_in, opp_pos );
        bool am_i_near = false;
        for ( int i = 0; i < (int)wm.teammatesFromBall().size() && i < 2; ++i ) {
            const auto * tm = wm.teammatesFromBall().at(i);
            if ( !tm || tm->unum() < 1 ) break;
            if ( tm->unum() == wm.self().unum() ) { am_i_near = true; break; }
        }
        if ( am_i_near && ball_opp_line.dist(self_pos) < 5.0 )
            target_pos = ball_in;
        else if ( ball_opp_line.dist(self_pos) > 5.0 ) {
            Vector2D proj = Segment2D(ball_in, target_pos).projection(self_pos);
            if ( proj.isValid() ) target_pos = proj;
        }
    }

    Segment2D opp_ball_seg( ball_in, opp_pos );
    if ( !opp_ball_seg.projection(self_pos).isValid() ) dist_thr = 0.1;

    double angle_thr = ( self_pos.dist(target_pos) > 2.0 ) ? 20.0 : 15.0;
    Body_GoToPoint( target_pos, dist_thr, dash_power,
                    -1.0, 100, false, angle_thr ).execute( agent );
}

// ── other_mark_power ──────────────────────────────────────────────────────────
double bhv_mark_execute::other_mark_power( PlayerAgent * agent,
                                            Vector2D opp_pos, Vector2D target_pos )
{
    const WorldModel & wm       = agent->world();
    int                opp_min   = wm.interceptTable().opponentStep();
    Vector2D           ball_in   = wm.ball().inertiaPoint( opp_min );
    double             dash_power = Strategy::get_normal_dash_power( wm );
    bool               is_back    = isBackRole( wm, wm.self().unum() );

    if ( opp_min < 5
      && !( ball_in.dist(target_pos) > 15.0 && ball_in.x > -25.0 && !is_back ) )
        dash_power = 100.0;
    if ( ball_in.x < wm.ourDefenseLineX() - 10.0 ) dash_power = 100.0;
    if ( target_pos.dist( Vector2D(-52.0,0.0) ) < 25.0 )
        if ( is_back || target_pos.dist(ball_in) < 20.0 ) dash_power = 100.0;
    if ( wm.interceptTable().firstOpponent()
      && wm.interceptTable().firstOpponent()->pos().dist(target_pos) < 5.0 )
        dash_power = 100.0;
    if ( target_pos.x < -35.0 ) dash_power = 100.0;
    if ( opp_min <= 2 ) dash_power = 100.0;

    return dash_power;
}

// ── other_mark_move ───────────────────────────────────────────────────────────
void bhv_mark_execute::other_mark_move( PlayerAgent * agent, Target targ,
                                         double dash_power, double dist_thr )
{
    const WorldModel & wm       = agent->world();
    int                opp_min   = wm.interceptTable().opponentStep();
    Vector2D           ball_in   = wm.ball().inertiaPoint( opp_min );
    Vector2D           self_pos  = wm.self().pos();
    Vector2D           target_pos = targ.pos;

    if ( ( self_pos.dist( Vector2D(-52.0, 0.0) ) < 25.0
        || ( self_pos.dist(target_pos) < 2.0 && ball_in.x < -30.0 )
        || ( fabs(target_pos.x - wm.ourDefenseLineX()) < 5.0 && target_pos.x < -35.0 ) )
      && wm.self().stamina() > 3000.0 ) {
        AngleDeg dir = ( target_pos - self_pos ).th() - wm.self().body();
        if ( target_pos.dist(self_pos) < 1.0
          || ( fabs(dir.degree()) < 10.0 && fabs(dir.degree()) > 170.0 ) ) {
            if ( agent->doDash( 100.0, dir ) ) return;
        }
    }

    Body_GoToPoint( target_pos, dist_thr, dash_power ).execute( agent );
}

// ── do_tackle ────────────────────────────────────────────────────────────────
bool bhv_mark_execute::do_tackle( PlayerAgent * agent )
{
    const WorldModel & wm = agent->world();
    if ( wm.self().pos().x < ServerParam::i().theirPenaltyAreaLineX() - 5.0
      && Bhv_BasicTackle( 0.9, 180.0 ).execute( agent ) ) return true;
    if ( Bhv_BasicTackle( 0.8, 180.0 ).execute( agent ) ) return true;
    if ( wm.self().pos().x < ServerParam::i().ourPenaltyAreaLineX() * 0.75
      && Bhv_BasicTackle( 0.7, 180.0 ).execute( agent ) ) return true;
    return false;
}

// ── back_to_def ──────────────────────────────────────────────────────────────
bool bhv_mark_execute::back_to_def( PlayerAgent * agent )
{
    const WorldModel & wm       = agent->world();
    Vector2D           ball_pos  = wm.ball().inertiaPoint( wm.interceptTable().opponentStep() );
    Vector2D           home_pos  = Strategy::i().getPosition( wm.self().unum() );
    Vector2D           self_pos  = wm.self().pos();
    double             def_line  = min( wm.ourDefenseLineX(), ball_pos.x );

    if ( wm.gameMode().type() != GameMode::PlayOn ) return false;
    if ( home_pos.x >= self_pos.x - 2.0 ) return false;

    if ( ball_pos.x > -35.0 && home_pos.x < def_line - 5.0 )
        return self_pos.dist(home_pos) > 5.0;
    else
        return self_pos.dist(home_pos) > 15.0;
}

// ── defenseBeInBack ──────────────────────────────────────────────────────────
bool bhv_mark_execute::defenseBeInBack( PlayerAgent * agent )
{
    const WorldModel & wm = agent->world();

    if ( BhvMarkDecisionGreedy::markDecision(wm) != MarkDec::MidMark )
        return false;

    Vector2D ball_pos        = wm.ball().inertiaPoint( wm.interceptTable().opponentStep() );
    double   tm_pos_def_line = ball_pos.x;
    double   tm_hpos_def_line = 0.0;

    for ( int i = 2; i <= 11; ++i ) {
        const AbstractPlayerObject * tm = wm.ourPlayer(i);
        if ( tm && tm->unum() > 0 && tm->pos().x < tm_pos_def_line )
            tm_pos_def_line = tm->pos().x;
        double hx = Strategy::i().getPosition(i).x;
        if ( hx < tm_hpos_def_line ) tm_hpos_def_line = hx;
    }

    if ( tm_pos_def_line >= tm_hpos_def_line - 5.0 ) return false;

    Vector2D target_point = Strategy::i().getPosition( wm.self().unum() );

    if ( isBackRole( wm, wm.self().unum() ) )
        target_point.x = tm_pos_def_line + 3.0;
    else if ( isHalfRole( wm, wm.self().unum() ) )
        target_point.x = tm_pos_def_line + 10.0;
    else
        target_point.x = tm_pos_def_line + 20.0;

    target_point.x = min( target_point.x,
                          Strategy::i().getPosition( wm.self().unum() ).x );

    double dist_thr  = max( 1.0, wm.ball().distFromSelf() * 0.1 );
    double dash_power = Strategy::get_normal_dash_power( wm );

    agent->debugClient().setTarget( target_point );
    agent->debugClient().addMessage( "BeInBack" );

    if ( !Body_GoToPoint( target_point, 0.5, dash_power ).execute( agent ) )
        Body_TurnToBall().execute( agent );

    setNeckAfterMark( agent );
    return true;
}

// ── defenseGoBack ─────────────────────────────────────────────────────────────
bool bhv_mark_execute::defenseGoBack( PlayerAgent * agent )
{
    const WorldModel & wm = agent->world();
    Vector2D ball_in  = wm.ball().inertiaPoint( wm.interceptTable().opponentStep() );
    double   def_line = wm.ourDefensePlayerLineX();

    if ( ball_in.absY() > 20.0 || ball_in.x > -5.0 ) return false;
    if ( wm.self().unum() > 5 ) return false;
    if ( ball_in.x >= def_line - 3.0 ) return false;

    const Vector2D targets_near[4] = {
        {-36.0,  3.0}, {-36.0, -8.0}, {-36.0, 8.0}, {-36.0, -3.0}
    };
    const Vector2D targets_far[4] = {
        {-49.0,  2.0}, {-49.0, -6.0}, {-49.0, 6.0}, {-49.0, -2.0}
    };
    int idx = wm.self().unum() - 2;
    if ( idx < 0 || idx > 3 ) return false;

    Vector2D target = ( ball_in.x > -30.0 ) ? targets_near[idx] : targets_far[idx];
    if ( target.dist( wm.self().pos() ) < 1.0 ) return false;

    agent->debugClient().addMessage( "GoBack" );
    if ( !Body_GoToPoint( target, 1.0, 100.0, 1.3, 1, false, 20.0 ).execute(agent) )
        Body_TurnToPoint( target, 1 ).execute( agent );

    agent->setNeckAction( new Neck_TurnToBall() );
    return true;
}

// ── change_position_set_play ──────────────────────────────────────────────────
Vector2D bhv_mark_execute::change_position_set_play( const WorldModel & wm,
                                                       Vector2D target )
{
    Vector2D ball = wm.ball().pos();
    if ( target.dist(ball) < 11.0 ) {
        AngleDeg away_dir = ( Vector2D(-52.5, 0.0) - target ).th();
        for ( int i = 1; i < 20; ++i ) {
            Vector2D cand = target + Vector2D::polar2vector( (double)i, away_dir );
            if ( cand.dist(ball) > 11.0 ) { target = cand; break; }
        }
    }
    return Bhv_SetPlay().get_avoid_circle_point( wm, target );
}
