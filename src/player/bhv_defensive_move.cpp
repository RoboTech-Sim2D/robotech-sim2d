// -*-c++-*-
// Ported from Cyrus OpenSource bhv_defensive_move.cpp (Phase 2)
// Adaptations:
//   PostLine::back/half/forward → roleNumber() ranges
//   Audio memory wait request   → removed (no equivalent in RoboTech)
//   bhv_mark_execute            → Bhv_MarkOpponent placeholder until Phase 4
//   Strategy::getNormalDashPower → Strategy::get_normal_dash_power

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "bhv_defensive_move.h"

#include "strategy.h"
#include "bhv_mark_execute.h"

#include "basic_actions/body_go_to_point.h"
#include "basic_actions/body_turn_to_point.h"
#include "basic_actions/neck_turn_to_ball_or_scan.h"
#include "basic_actions/neck_turn_to_ball.h"

#include <rcsc/player/player_agent.h>
#include <rcsc/player/world_model.h>
#include <rcsc/player/intercept_table.h>
#include <rcsc/player/debug_client.h>
#include <rcsc/common/logger.h>
#include <rcsc/geom/vector_2d.h>

#include <algorithm>
#include <cmath>

using namespace rcsc;

namespace {
    inline int roleOf( const WorldModel & wm ) {
        return Strategy::i().roleNumber( wm.self().unum() );
    }
    inline bool isBack( const WorldModel & wm ) {
        int r = roleOf( wm );
        return r >= 2 && r <= 5;
    }
    inline bool isHalf( const WorldModel & wm ) {
        int r = roleOf( wm );
        return r >= 6 && r <= 8;
    }
    inline bool isForward( const WorldModel & wm ) {
        int r = roleOf( wm );
        return r >= 9 && r <= 11;
    }
}

bool Bhv_DefensiveMove::execute( rcsc::PlayerAgent * agent )
{
    const WorldModel & wm = agent->world();
    const int opp_min = wm.interceptTable().opponentStep();

    if ( wm.interceptTable().firstOpponent() == nullptr
      || wm.interceptTable().firstOpponent()->unum() < 1 ) {
        return false;
    }

    // Phase 4 will replace this with bhv_mark_execute.
    // For now, use Bhv_MarkOpponent as the marking action.
    bool mark_or_block = true;
    if ( isForward( wm ) ) {
        if ( wm.ball().pos().x < -20.0 ) {
            if ( wm.self().stamina() < 4000.0 ) mark_or_block = false;
        } else {
            if ( wm.self().stamina() < 5000.0 ) mark_or_block = false;
        }
    }

    if ( mark_or_block ) {
        if ( bhv_mark_execute().execute( agent ) ) {
            agent->debugClient().addMessage( "DefMoveMarkEx" );
            return true;
        }
    }

    // --- Fallback: go to home position with Cyrus smart dash power ---

    // Lowest home-position x among field players
    double min_x_strategy = 100.0;
    for ( int i = 2; i <= 11; ++i ) {
        double x = Strategy::i().getPosition( i ).x;
        if ( x < min_x_strategy ) min_x_strategy = x;
    }

    Vector2D inertia_ball = wm.ball().inertiaPoint( opp_min );
    Vector2D target_point = Strategy::i().getPosition( wm.self().unum() );

    // Conserve stamina: caps forward half if ball is in our half and stamina low
    if ( wm.self().unum() >= 2 && wm.self().unum() <= 4 ) {
        if ( inertia_ball.x > 0.0 && wm.self().pos().x < 0.0
          && wm.self().stamina() < 6000.0 ) {
            target_point.x = std::min( target_point.x, -1.0 );
        }
    }

    double dash_power = Strategy::get_normal_dash_power( wm );

    // Full power when ball is behind us (backs chase)
    if ( wm.ball().pos().x < wm.self().pos().x && isBack( wm ) ) {
        dash_power = 100.0;
    }

    // Full power when ball is deep in our half and far from target
    if ( isBack( wm ) || isHalf( wm ) ) {
        if ( inertia_ball.x < -20.0
          && ( wm.self().pos().dist( target_point ) > 4.0
            || wm.self().pos().x > target_point.x + 2.0 ) ) {
            dash_power = 100.0;
        }
    }

    // Always full power when behind all our home positions
    if ( wm.self().pos().x < min_x_strategy ) {
        dash_power = 100.0;
    }

    // Full power when back line under pressure
    if ( isBack( wm )
      && std::abs( inertia_ball.x - wm.ourDefenseLineX() ) < 20.0 ) {
        dash_power = 100.0;
    }

    double dist_thr = std::max( 1.0, wm.ball().distFromSelf() * 0.1 );

    dlog.addText( Logger::TEAM,
                  __FILE__": Bhv_DefensiveMove target=(%.1f %.1f) dash=%.0f",
                  target_point.x, target_point.y, dash_power );

    agent->debugClient().addMessage( "DefMove%.0f", dash_power );
    agent->debugClient().setTarget( target_point );

    if ( ! Body_GoToPoint( target_point, dist_thr, dash_power ).execute( agent ) ) {
        Body_TurnToPoint( target_point, 1 ).execute( agent );
    }

    // Neck: prefer looking at ball; scan if ball is well-tracked
    if ( wm.kickableOpponent() && wm.ball().distFromSelf() < 18.0 ) {
        agent->setNeckAction( new Neck_TurnToBall() );
    } else {
        agent->setNeckAction( new Neck_TurnToBallOrScan( 0 ) );
    }

    return true;
}
