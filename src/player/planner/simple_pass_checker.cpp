// -*-c++-*-

/*
 *Copyright:

 Copyright (C) Hiroki SHIMORA, Hidehisa AKIYAMA

 This code is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3, or (at your option)
 any later version.

 This code is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this code; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *EndCopyright:
 */

/////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "simple_pass_checker.h"

#include "predict_state.h"

#include <rcsc/player/world_model.h>
#include <rcsc/common/server_param.h>
#include <rcsc/common/logger.h>

using namespace rcsc;

static const double GOALIE_PASS_EVAL_THRESHOLD = 17.5;
static const double BACK_PASS_EVAL_THRESHOLD = 10.0;   // permissive: back passes under pressure are safe
static const double PASS_EVAL_THRESHOLD = 11.0;        // was 14.5 — too tight, blocked valid forward passes
static const double CHANCE_PASS_EVAL_THRESHOLD = 11.0; // was 14.5
static const double PASS_RECEIVER_PREDICT_STEP = 2.5;

static const double PASS_REFERENCE_SPEED = 2.5;

static const double PASS_SPEED_THRESHOLD = 1.5;

static const double NEAR_PASS_DIST_THR = 4.0;
static const double FAR_PASS_DIST_THR = 35.0;

static const long VALID_TEAMMATE_ACCURACY = 8;
static const long VALID_OPPONENT_ACCURACY = 20;
// Zone-based: in opponent box marking is tight — only block if opponent is ON receiver.
// In midfield be slightly more permissive than before (was 5.0, then 3.0).
// Value set dynamically below based on receive_point.x.
static const double OPPONENT_DIST_THR2 = std::pow( 3.0, 2 );  // default (unused — see below)

/*-------------------------------------------------------------------*/
/*!

 */
bool
SimplePassChecker::operator()( const PredictState & state,
                               const AbstractPlayerObject & from,
                               const AbstractPlayerObject & to,
                               const Vector2D & receive_point,
                               const double & first_ball_speed ) const
{
    //
    // inhibit self pass
    //
    if ( from.unum() == to.unum() )
    {
        return false;
    }

    if ( first_ball_speed < PASS_SPEED_THRESHOLD )
    {
        return false;
    }

    if ( from.isGhost()
         || to.isGhost()
         || from.posCount() > VALID_TEAMMATE_ACCURACY
         || to.posCount() > VALID_TEAMMATE_ACCURACY )
    {
        return false;
    }

    const Vector2D from_pos = ( from.isSelf()
                                ? state.ball().pos()
                                : from.pos() );
    const double pass_dist = from_pos.dist( receive_point );

    if ( pass_dist <= NEAR_PASS_DIST_THR )
    {
#ifdef DEBUG_PRINT
        dlog.addText( Logger::PASS,
                      "(SimplePassChecker) %d to %d: (%.2f %.2f)->(%.2f %.2f) too near. dist=%.2f",
                      from.unum(), to.unum(),
                      from_pos.x, from_pos.y,
                      receive_point.x, receive_point.y,
                      pass_dist );
#endif
        return false;
    }

    if ( pass_dist >= FAR_PASS_DIST_THR )
    {
#ifdef DEBUG_PRINT
        dlog.addText( Logger::PASS,
                      "(SimplePassChecker) %d to %d: (%.2f %.2f)->(%.2f %.2f) too far. dist=%.2f",
                      from.unum(), to.unum(),
                      from_pos.x, from_pos.y,
                      receive_point.x, receive_point.y,
                      pass_dist );
#endif
        return false;
    }

    if ( to.pos().x >= state.offsideLineX() )
    {
#ifdef DEBUG_PRINT
        dlog.addText( Logger::PASS,
                      "(SimplePassChecker) %d to %d: (%.2f %.2f)->(%.2f %.2f) offsideX=%.2f",
                      from.unum(), to.unum(),
                      from_pos.x, from_pos.y,
                      receive_point.x, receive_point.y,
                      state.offsideLineX() );
#endif
        return false;
    }

    // Only block passes INTO our penalty area if the receiver is stationary there
    // (no build-up possible). Allow passes near the edge for build-up play.
    if ( receive_point.x <= ServerParam::i().ourPenaltyAreaLineX() + 1.0
         && receive_point.absY() <= ServerParam::i().penaltyAreaHalfWidth() + 1.0 )
    {
#ifdef DEBUG_PRINT
        dlog.addText( Logger::PASS,
                      "(SimplePassChecker) %d to %d: (%.2f %.2f)->(%.2f %.2f) in penalty area",
                      from.unum(), to.unum(),
                      from_pos.x, from_pos.y,
                      receive_point.x, receive_point.y );
#endif
        return false;
    }

    if ( receive_point.absX() >= ServerParam::i().pitchHalfLength()
         || receive_point.absY() >= ServerParam::i().pitchHalfWidth() )
    {
#ifdef DEBUG_PRINT
        dlog.addText( Logger::PASS,
                      "(SimplePassChecker) %d to %d: (%.2f %.2f)->(%.2f %.2f) out of field",
                      from.unum(), to.unum(),
                      from_pos.x, from_pos.y,
                      receive_point.x, receive_point.y );
#endif
        return false;
    }

    // MEJORADO: Protección absoluta contra pases al portero (back pass rule)
    if ( to.goalie() )
    {
#ifdef DEBUG_PRINT
        dlog.addText( Logger::PASS,
                      "(SimplePassChecker) %d to %d: BLOCKED - never pass to goalie (back pass rule)",
                      from.unum(), to.unum() );
#endif
        return false;
    }

    const double receiver_move_dist = to.pos().dist( receive_point );

    const AngleDeg pass_angle = ( receive_point - from_pos ).th();
    const double OVER_TEAMMATE_IGNORE_DISTANCE2
        = std::pow( pass_dist + ( receive_point.x >= +25.0 ? 2.0 : 6.0 ), 2 );

    double pass_course_cone = + 360.0;

    for ( PlayerObject::Cont::const_iterator o = state.opponentsFromSelf().begin(),
              end = state.opponentsFromSelf().end();
          o != end;
          ++o )
    {
        if ( (*o)->posCount() > VALID_OPPONENT_ACCURACY )
        {
            continue;
        }

        // Zone-based opponent proximity threshold:
        // In the box (x>25): allow passes to marked players — 1.5m threshold.
        // In opponent half (x>0): moderate — 2.0m threshold.
        // In our half: conservative — 3.0m threshold.
        double opp_dist_thr2;
        if ( receive_point.x > 25.0 )
            opp_dist_thr2 = std::pow( 1.0, 2 );  // box: 1.0m — central striker always marked
        else if ( receive_point.x > 0.0 )
            opp_dist_thr2 = std::pow( 2.0, 2 );
        else
            opp_dist_thr2 = std::pow( 3.0, 2 );

        if ( ( (*o)->pos() - receive_point ).r2() < opp_dist_thr2 )
        {
            return false;
        }

        Vector2D opp_pos = (*o)->inertiaFinalPoint();

        const double opp_dist2 = from_pos.dist2( opp_pos );

        if ( opp_dist2 > OVER_TEAMMATE_IGNORE_DISTANCE2 )
        {
            continue;
        }

        const double opp_move_dist = opp_pos.dist( receive_point );

        if ( opp_move_dist < receiver_move_dist * 0.85 )
        {
            return false;
        }

        double angle_diff = ( ( opp_pos - from_pos ).th() - pass_angle ).abs();

        if ( from.isSelf() )
        {
            const double control_area = (*o)->playerTypePtr()->kickableArea();
            const double hide_radian = std::asin( std::min( control_area / std::sqrt( opp_dist2 ),
                                                      1.0 ) );
            angle_diff = std::max( angle_diff - AngleDeg::rad2deg( hide_radian ), 0.0 );
        }

        if ( pass_course_cone > angle_diff )
        {
            pass_course_cone = angle_diff;
        }
    }


    // Dynamic threshold: short passes need less open corridor (receiver gets ball
    // quickly before opponents react); long passes need more open path.
    // Formula: base scales with distance, capped at min/max.
    double eval_threshold;
    if ( pass_dist < 8.0 )
        eval_threshold = 7.0;   // short pass — generous
    else if ( pass_dist < 18.0 )
        eval_threshold = 7.0 + ( pass_dist - 8.0 ) * 0.35;  // 7..10.5°
    else
        eval_threshold = 10.5 + ( pass_dist - 18.0 ) * 0.15; // 10.5..13°

    // Offensive zone bonus: receiving in opponent half → less strict
    if ( receive_point.x > 20.0 )
        eval_threshold = std::max( eval_threshold - 2.0, 5.0 );

    // Back pass: more permissive (defensive safety valve)
    const double BACK_PASS_X_MARGIN = 1.0;
    if ( to.pos().x + BACK_PASS_X_MARGIN <= from.pos().x )
    {
        eval_threshold = BACK_PASS_EVAL_THRESHOLD;
#ifdef DEBUG_PRINT
        dlog.addText( Logger::PASS,
                      "(SimplePassChecker) back pass detected: from(%.1f) to(%.1f)",
                      from.pos().x, to.pos().x );
#endif
    }

    if ( from.pos().x >= +25.0
         && to.pos().x >= +25.0 )
    {
        eval_threshold = CHANCE_PASS_EVAL_THRESHOLD;
    }

    if ( from.goalie() )
    {
        eval_threshold = GOALIE_PASS_EVAL_THRESHOLD;
    }


    // adjust angle threshold by ball speed
    eval_threshold *= ( PASS_REFERENCE_SPEED / first_ball_speed );

    if ( pass_course_cone <= eval_threshold )
    {
#ifdef DEBUG_PRINT
        dlog.addText( Logger::PASS,
                      "(SimplePassChecker) %d to %d: (%.2f %.2f)->(%.2f %.2f) too narrow. angleWidth=%.3f",
                      from.unum(), to.unum(),
                      from_pos.x, from_pos.y,
                      receive_point.x, receive_point.y,
                      pass_course_cone );
#endif
        return false;
    }

#ifdef DEBUG_PRINT
    dlog.addText( Logger::PASS,
                  "ok (SimplePassChecker) %d to %d: (%.2f %.2f)->(%.2f %.2f) angleWidth=%.3f",
                  from.unum(), to.unum(),
                  from_pos.x, from_pos.y,
                  receive_point.x, receive_point.y,
                  pass_course_cone );
#endif
    return true;
}
