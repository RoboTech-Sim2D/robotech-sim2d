// -*-c++-*-

/*
 *Copyright:

 Copyright (C) Hidehisa AKIYAMA

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

#include "bhv_goalie_basic_move.h"

#include "bhv_basic_tackle.h"
#include "neck_goalie_turn_neck.h"
#include "bhv_goalie_chase_ball.h"


#include "basic_actions/basic_actions.h"
#include "basic_actions/body_go_to_point.h"
#include "basic_actions/body_stop_dash.h"
#include "basic_actions/bhv_go_to_point_look_ball.h"

#include <rcsc/player/player_agent.h>
#include <rcsc/player/intercept_table.h>
#include <rcsc/player/debug_client.h>

#include <rcsc/common/logger.h>
#include <rcsc/common/server_param.h>
#include <rcsc/geom/line_2d.h>
#include <rcsc/soccer_math.h>

using namespace rcsc;

/*-------------------------------------------------------------------*/
/*!

 */
bool Bhv_GoalieBasicMove::execute( PlayerAgent * agent )
{
    const WorldModel & wm = agent->world();
    const ServerParam & SP = ServerParam::i();

    // ---------------------------------------------------------------
    // isDanger flag (Cyrus-style): suppress aggressive GK behavior when a
    // teammate just kicked. Prevents the GK from abandoning goal during our
    // own attacks or after a back-pass.  Cleared after 33 cycles (ball dies
    // down) or when someone becomes kickable.
    static bool s_is_danger   = false;
    static int  s_danger_cycle = 0;
    const  int  current_cycle  = wm.time().cycle();

    if ( wm.lastKickerSide() == wm.ourSide() )
    {
        s_is_danger    = true;
        s_danger_cycle = current_cycle;
    }
    if ( s_is_danger )
    {
        if ( wm.gameMode().type() != GameMode::PlayOn )
            s_is_danger = false;
        if ( current_cycle - s_danger_cycle > 33
             && wm.ball().vel().r() > 1.0 )
            s_is_danger = false;
        if ( wm.kickableOpponent() || wm.kickableTeammate() )
            s_is_danger = false;
    }

    // ---------------------------------------------------------------
    // PRIORITY 0: Tackle if possible
    if ( Bhv_BasicTackle( 1.0, 98.0 ).execute( agent ) ) return true;

    // ---------------------------------------------------------------
    // PRIORITY 1: Intercept / react based on WHO has the ball
    // Solo con balón vivo: en faltas rivales cerca del área el pateador
    // junto al balón cuenta como kickableOpponent y la rama (A) mandaba
    // al GK a perseguir un balón muerto — salía y regresaba en bucle.
    if ( wm.gameMode().type() == GameMode::PlayOn )
    {
        const int self_step = wm.interceptTable().selfStep();
        const int tm_step   = wm.interceptTable().teammateStep();
        const int opp_step  = wm.interceptTable().opponentStep();

        const Vector2D ball_pos = wm.ball().pos();
        const bool ball_in_area = ( ball_pos.x < SP.ourPenaltyAreaLineX() + 3.0
                                    && ball_pos.absY() < SP.penaltyAreaHalfWidth() + 3.0 );
        const bool ball_coming = ( wm.ball().vel().x < -0.2 );

        const bool opp_has_ball = ( wm.kickableOpponent() != nullptr );
        const bool tm_has_ball  = ( wm.kickableTeammate() != nullptr );
        const bool ball_is_loose = ( ! opp_has_ball && ! tm_has_ball );

        // Cobertura propia sobre el balón: con defensas encima del balón,
        // salir solo regala la portería. El GK únicamente sale sin importar
        // la cobertura cuando el balón está en la puerta de su arco.
        int mates_on_ball = 0;
        for ( const PlayerObject * tm : wm.teammatesFromBall() )
        {
            if ( ! tm || tm->goalie() ) continue;
            if ( tm->distFromBall() > 5.0 ) break;  // lista ordenada por distancia
            ++mates_on_ball;
        }
        const bool ball_in_goal_area =
            ( ball_pos.x < -SP.pitchHalfLength() + SP.goalAreaLength() + 2.0
              && ball_pos.absY() < SP.goalAreaWidth() * 0.5 + 3.0 );

        // (A) OPPONENT has ball in/near our penalty area → COME OUT
        //     SOLO si es un 1v1 real (nadie nuestro sobre el balón) o el
        //     balón ya está en el área de meta. Antes salía siempre, incluso
        //     con 3 compañeros presionando, y dejaba el arco vacío.
        if ( opp_has_ball && ball_in_area
             && ( mates_on_ball == 0 || ball_in_goal_area ) )
        {
            dlog.addText( Logger::TEAM, __FILE__": GK rush opp in area (1v1)" );
            agent->debugClient().addMessage( "GK_RushOpp" );
            if ( Bhv_GoalieChaseBall().execute( agent ) ) return true;
        }

        // (B) Ball is LOOSE in the area (cross, deflection, failed pass)
        //     → salir solo si llega ANTES que el rival y que el compañero.
        //     El margen +5 anterior lo hacía salir llegando tarde: el rival
        //     recibía con el GK a medio camino (de ahí el "no reacciona").
        if ( ball_is_loose && ball_in_area
             && ( ( self_step <= opp_step - 1 && self_step <= tm_step )
                  || ball_in_goal_area ) )
        {
            dlog.addText( Logger::TEAM, __FILE__": GK intercept loose ball" );
            agent->debugClient().addMessage( "GK_Loose" );
            if ( Bhv_GoalieChaseBall().execute( agent ) ) return true;
        }

        // (B2) Lead-pass aggression: loose ball rolling fast toward our area.
        //      Cyrus rule: only if GK reaches BEFORE teammate (don't abandon
        //      goal if a teammate handles it), and not while team is attacking
        //      (isDanger — ball was just kicked by us).
        if ( !s_is_danger )
        {
            const Vector2D b_vel = wm.ball().vel();
            const double penalty_x = SP.ourPenaltyAreaLineX();  // ≈ -36
            if ( ball_is_loose
                 && ball_pos.x < -22.0
                 && ball_pos.x > penalty_x - 2.0   // outside or barely at area edge
                 && b_vel.x < -0.5
                 && b_vel.r() > 0.8
                 && self_step <= opp_step - 1      // gana la carrera, no la empata
                 && self_step < tm_step )           // Cyrus: only if GK is fastest
            {
                dlog.addText( Logger::TEAM, __FILE__": GK lead-pass aggression" );
                agent->debugClient().addMessage( "GK_LeadPass" );
                if ( Bhv_GoalieChaseBall().execute( agent ) ) return true;
            }
        }

        // (C) Ball heading toward goal — chase it (also suppressed during isDanger)
        if ( !s_is_danger
             && ball_coming && ball_pos.x < -20.0
             && self_step <= opp_step )
        {
            dlog.addText( Logger::TEAM, __FILE__": GK chase incoming" );
            agent->debugClient().addMessage( "GK_Chase" );
            if ( Bhv_GoalieChaseBall().execute( agent ) ) return true;
        }

        // (D) Standard intercept (nobody has ball, ball near area)
        //     Solo si gana claramente la carrera o el balón está en su puerta.
        if ( ball_in_area
             && ( ( self_step < opp_step && self_step <= tm_step )
                  || ball_in_goal_area ) )
        {
            dlog.addText( Logger::TEAM, __FILE__": GK intercept standard" );
            agent->debugClient().addMessage( "GK_Intercept" );
            if ( Bhv_GoalieChaseBall().execute( agent ) ) return true;
        }

        // NOTE: if TEAMMATE has ball → do nothing here, fall through to
        // positioning (Priority 2). GK stays in goal, no need to come out.
    }

    // ---------------------------------------------------------------
    // PRIORITY 2: ALWAYS move to target at MAXIMUM SPEED
    // The GK must never conserve stamina — it barely moves, so stamina
    // is never a problem. Using anything less than max power makes the
    // GK look "frozen" because it takes too many cycles to reposition.
    const Vector2D move_point = getTargetPoint( agent );
    dlog.addText( Logger::TEAM,
                  __FILE__": GK target(%.2f %.2f)", move_point.x, move_point.y );

    {
        const double dist_to_target = wm.self().pos().dist( move_point );

        if ( dist_to_target > 0.3 )
        {
            agent->debugClient().addMessage( "GK_Sprint" );
            agent->debugClient().setTarget( move_point );
            Body_GoToPoint( move_point, 0.3, SP.maxDashPower() ).execute( agent );
            agent->setNeckAction( new Neck_GoalieTurnNeck() );
            return true;
        }
    }

    // Already at target — face sideways to be ready for lateral dashes
    {
        const Vector2D ball_next = wm.ball().pos() + wm.ball().vel();
        const AngleDeg target_angle = ( ball_next.y < 0.0 ? -90.0 : 90.0 );
        Body_TurnToAngle( target_angle ).execute( agent );
        agent->setNeckAction( new Neck_GoalieTurnNeck() );
    }

    return true;
}



/*-------------------------------------------------------------------*/
/*!

 */
Vector2D
Bhv_GoalieBasicMove::getTargetPoint( PlayerAgent * agent )
{
    const ServerParam & SP = ServerParam::i();
    const WorldModel & wm = agent->world();

    const double goal_half_w = SP.goalHalfWidth();       // 7.32/2 = 3.66
    const double pitch_half_l = SP.pitchHalfLength();     // 52.5
    const Vector2D goal_center( -pitch_half_l, 0.0 );

    // ------------------------------------------------------------------
    // 1. Predict where the ball will be
    // ------------------------------------------------------------------
    int ball_reach_step = 0;
    if ( ! wm.kickableTeammate() && ! wm.kickableOpponent() )
    {
        ball_reach_step = std::min( wm.interceptTable().teammateStep(),
                                    wm.interceptTable().opponentStep() );
    }
    const Vector2D ball_pos = wm.ball().inertiaPoint( ball_reach_step );

    // ------------------------------------------------------------------
    // 2. Dynamic base_move_x
    //    Ball far away (x>0)  → GK at -46 (moderate advance)
    //    Ball in midfield     → interpolate -46 to -49
    //    Ball in penalty area → -49 to -50 (hug goal line)
    // ------------------------------------------------------------------
    double base_move_x;
    if ( ball_pos.x > 0.0 )
    {
        base_move_x = -46.0;
    }
    else if ( ball_pos.x > SP.ourPenaltyAreaLineX() )  // > -36
    {
        double ratio = ( -ball_pos.x ) / 36.0;  // 0..1
        base_move_x = -46.0 - 3.0 * ratio;      // -46..-49
    }
    else
    {
        // Ball inside/near penalty area
        double depth = std::min( -ball_pos.x - 36.0, 16.5 ) / 16.5; // 0..1
        base_move_x = -49.0 - 1.0 * depth;  // -49..-50
        base_move_x = std::max( base_move_x, -pitch_half_l + 0.5 );
    }

    // ------------------------------------------------------------------
    // 2b. 1v1 angle-closing: opponent has ball outside area but threatening.
    //     Default base_move_x (-47 to -50) leaves a huge shooting angle.
    //     Advance GK to close it. Only when truly 1v1 (no defender between).
    // ------------------------------------------------------------------
    if ( wm.gameMode().type() == GameMode::PlayOn  // balón muerto no es 1v1:
         // en una falta el "kickableOpponent" es el pateador y los defensas
         // están a 9.15m por regla, así que nunca hay "defender cover"
         && wm.kickableOpponent()
         && ball_pos.x > SP.ourPenaltyAreaLineX()  // outside penalty area
         && ball_pos.x < -15.0                      // within shooting range
         && ball_pos.absY() < SP.penaltyAreaHalfWidth() + 2.0 )
    {
        bool has_defender_cover = false;
        for ( const PlayerObject * tm : wm.teammates() )
        {
            if ( ! tm || tm->goalie() ) continue;
            // Defender is between ball and goal and closer to goal than ball
            if ( tm->pos().x < ball_pos.x && tm->pos().x > -45.0
                 && tm->pos().dist( ball_pos ) < 8.0 )
            {
                has_defender_cover = true;
                break;
            }
        }
        if ( ! has_defender_cover )
        {
            // Advance to close angle — x=-43 cuts shooting angle significantly
            base_move_x = std::max( base_move_x, -43.0 );
        }
    }

    // ------------------------------------------------------------------
    // 3. Near-post coverage: when ball is wide (large |Y|) and deep,
    //    the GK must move to the near post, not stay in the center.
    //    This is the #1 cause of goals against us.
    // ------------------------------------------------------------------
    if ( ball_pos.x < -36.0 && ball_pos.absY() > goal_half_w + 2.0 )
    {
        // Ball is deep and wide — cover near post
        double post_y = ( ball_pos.y > 0.0 )
                        ? goal_half_w - 0.3
                        : -( goal_half_w - 0.3 );
        double post_x = std::max( base_move_x, -pitch_half_l + 0.5 );

        agent->debugClient().addMessage( "GK_NearPost" );
        dlog.addText( Logger::TEAM, __FILE__": getTarget near-post (%.1f, %.1f)",
                      post_x, post_y );
        return Vector2D( post_x, post_y );
    }

    // ------------------------------------------------------------------
    // 4. Very wide ball (outside goal width but not deep) — go to post
    // ------------------------------------------------------------------
    if ( ball_pos.absY() > goal_half_w + 5.0 )
    {
        double post_y = ( ball_pos.y > 0.0 )
                        ? goal_half_w - 0.2
                        : -( goal_half_w - 0.2 );
        agent->debugClient().addMessage( "GK_WideBall" );
        return Vector2D( std::max( base_move_x, -pitch_half_l + 0.5 ), post_y );
    }

    // ------------------------------------------------------------------
    // 5. Normal case: position on ball trajectory or bisecting line
    // ------------------------------------------------------------------
    {
        const double y_buf = 0.3;

        // ── RoboCIn 2024: Orthogonal projection to ball trajectory ──
        // When ball is moving toward our goal, position at the point where
        // the ball's trajectory crosses our GK depth line (base_move_x).
        // This is more accurate than the static bisecting-line approach
        // because it accounts for ball direction, not just ball position.
        const Vector2D ball_vel = wm.ball().vel();
        const double ball_speed = ball_vel.r();
        if ( ball_vel.x < -0.3 && ball_speed > 0.5
             && ball_pos.x > base_move_x )
        {
            // Find t > 0 where ball crosses x = base_move_x
            double t = ( base_move_x - ball_pos.x ) / ball_vel.x;
            if ( t > 0.0 && t < 80.0 )
            {
                double traj_y = ball_pos.y + ball_vel.y * t;

                // Also compute bisecting-line Y (fallback for slow balls)
                const double x_back = 3.5;
                const Vector2D base_point( -pitch_half_l - x_back, 0.0 );
                Vector2D ball_point_bis = ball_pos;
                if ( ball_point_bis.x < base_point.x + 0.1 )
                    ball_point_bis.x = base_point.x + 0.1;
                Line2D ball_line_bis( ball_point_bis, base_point );
                double bisect_y = ball_line_bis.getY( base_move_x );

                // Blend: fast ball → trust trajectory fully; slow ball → bisect
                double traj_w = std::min( 1.0, ( ball_speed - 0.5 ) / 1.5 );
                double move_y = traj_y * traj_w + bisect_y * ( 1.0 - traj_w );

                if ( move_y > goal_half_w - y_buf )  move_y = goal_half_w - y_buf;
                if ( move_y < -goal_half_w + y_buf ) move_y = -goal_half_w + y_buf;

                agent->debugClient().addMessage( "GK_OrthoProj" );
                dlog.addText( Logger::TEAM,
                              __FILE__": getTarget ortho-proj t=%.1f y=%.2f",
                              t, move_y );
                return Vector2D( base_move_x, move_y );
            }
        }

        // ── Standard bisecting line (ball static or moving away) ──
        //    GK stands where the line from ball to virtual point behind goal
        //    crosses base_move_x, blended with direct Y-tracking for far balls.
        const double x_back = 3.5;  // virtual point behind goal
        const Vector2D base_point( -pitch_half_l - x_back, 0.0 );

        Vector2D ball_point;
        if ( wm.kickableOpponent() )
        {
            ball_point = ball_pos;
        }
        else
        {
            int pred_cycle = std::min( 8,
                             wm.interceptTable().opponentStep() );
            ball_point = inertia_n_step_point( ball_pos,
                                               wm.ball().vel(),
                                               pred_cycle,
                                               SP.ballDecay() );
        }

        if ( ball_point.x < base_point.x + 0.1 )
        {
            ball_point.x = base_point.x + 0.1;
        }

        Line2D ball_line( ball_point, base_point );
        double line_y = ball_line.getY( base_move_x );

        // Far ball → blend with direct ball-Y tracking to avoid GK looking "frozen"
        double direct_y = ball_pos.y * 0.45;
        double ball_dist_x = std::fabs( ball_pos.x - base_move_x );
        double blend = std::min( 1.0, std::max( 0.0, (ball_dist_x - 15.0) / 30.0 ) );
        double move_y = line_y * (1.0 - blend) + direct_y * blend;

        if ( move_y > goal_half_w - y_buf )  move_y = goal_half_w - y_buf;
        if ( move_y < -goal_half_w + y_buf ) move_y = -goal_half_w + y_buf;

        return Vector2D( base_move_x, move_y );
    }
}

/*-------------------------------------------------------------------*/
/*!

 */
double
Bhv_GoalieBasicMove::getBasicDashPower( PlayerAgent * agent,
                                        const Vector2D & move_point )
{
    const WorldModel & wm = agent->world();
    const PlayerType & mytype = wm.self().playerType();

    const double my_inc = mytype.staminaIncMax() * wm.self().recovery();

    if ( std::fabs( wm.self().pos().x - move_point.x ) > 3.0 )
    {
        return ServerParam::i().maxDashPower();
    }

    // Ball coming fast toward goal → always max power to reposition
    if ( wm.ball().vel().x < -0.5 && wm.ball().pos().x < -15.0 )
    {
        return ServerParam::i().maxDashPower();
    }

    if ( wm.ball().pos().x > -30.0 )
    {
        if ( wm.self().stamina() < ServerParam::i().staminaMax() * 0.9 )
        {
            return my_inc * 0.7;  // was 0.5 — faster repositioning when ball is far
        }
        agent->debugClient().addMessage( "P1" );
        return my_inc;
    }
    else if ( wm.ball().pos().x > ServerParam::i().ourPenaltyAreaLineX() )
    {
        if ( wm.ball().pos().absY() > 20.0 )
        {
            // penalty area
            agent->debugClient().addMessage( "P2" );
            return my_inc;
        }
        if ( wm.ball().vel().x > 1.0 )
        {
            // ball is moving to opponent side
            agent->debugClient().addMessage( "P2.5" );
            return my_inc * 0.5;
        }

        int opp_min = wm.interceptTable().opponentStep();
        if ( opp_min <= 3 )
        {
            agent->debugClient().addMessage( "P2.3" );
            return ServerParam::i().maxDashPower();
        }

        if ( wm.self().stamina() < ServerParam::i().staminaMax() * 0.7 )
        {
            agent->debugClient().addMessage( "P2.6" );
            return my_inc * 0.7;
        }
        agent->debugClient().addMessage( "P3" );
        return ServerParam::i().maxDashPower() * 0.6;
    }
    else
    {
        if ( wm.ball().pos().absY() < 15.0
             || wm.ball().pos().y * wm.self().pos().y < 0.0 ) // opposite side
        {
            agent->debugClient().addMessage( "P4" );
            return ServerParam::i().maxDashPower();
        }
        else
        {
            agent->debugClient().addMessage( "P5" );
            return my_inc;
        }
    }
}

/*-------------------------------------------------------------------*/
/*!

 */
bool
Bhv_GoalieBasicMove::doPrepareDeepCross( PlayerAgent * agent,
                                         const Vector2D & move_point )
{
    if ( move_point.absY() < ServerParam::i().goalHalfWidth() - 0.8 )
    {
        // consider only very deep cross
        dlog.addText( Logger::TEAM,
                      __FILE__": doPrepareDeepCross no deep cross" );
        return false;
    }

    const WorldModel & wm = agent->world();

    const Vector2D goal_c( - ServerParam::i().pitchHalfLength(), 0.0 );

    Vector2D goal_to_ball = wm.ball().pos() - goal_c;

    if ( goal_to_ball.th().abs() < 60.0 )
    {
        // ball is not in side cross area
        dlog.addText( Logger::TEAM,
                      __FILE__": doPrepareDeepCross.ball is not in side cross area" );
        return false;
    }

    Vector2D my_inertia = wm.self().inertiaFinalPoint();
    double dist_thr = wm.ball().distFromSelf() * 0.1;
    if ( dist_thr < 0.5 ) dist_thr = 0.5;
    //double dist_thr = 0.5;

    if ( my_inertia.dist( move_point ) > dist_thr )
    {
        // needed to go to move target point
        double dash_power = getBasicDashPower( agent, move_point );
        dlog.addText( Logger::TEAM,
                      __FILE__": doPrepareDeepCross. need to move. power=%.1f",
                      dash_power );
        agent->debugClient().addMessage( "DeepCrossMove%.0f", dash_power );
        agent->debugClient().setTarget( move_point );
        agent->debugClient().addCircle( move_point, dist_thr );

        doGoToPointLookBall( agent,
                             move_point,
                             wm.ball().angleFromSelf(),
                             dist_thr,
                             dash_power );
        return true;
    }

    AngleDeg body_angle = ( wm.ball().pos().y < 0.0
                            ? 10.0
                            : -10.0 );
    agent->debugClient().addMessage( "PrepareCross" );
    dlog.addText( Logger::TEAM,
                  __FILE__": doPrepareDeepCross  body angle = %.1f  move_point(%.1f %.1f)",
                  body_angle.degree(),
                  move_point.x, move_point.y );
    agent->debugClient().setTarget( move_point );

    Body_TurnToAngle( body_angle ).execute( agent );
    agent->setNeckAction( new Neck_GoalieTurnNeck() );
    return true;
}

/*-------------------------------------------------------------------*/
/*!

 */
bool
Bhv_GoalieBasicMove::doStopAtMovePoint( PlayerAgent * agent,
                                        const Vector2D & move_point )
{
    //----------------------------------------------------------
    // already exist at target point
    // but inertia movement is big
    // stop dash

    const WorldModel & wm = agent->world();
    double dist_thr = wm.ball().distFromSelf() * 0.1;
    if ( dist_thr < 0.5 ) dist_thr = 0.5;

    // now, in the target area
    if ( wm.self().pos().dist( move_point ) < dist_thr )
    {
        const Vector2D my_final
            = inertia_final_point( wm.self().pos(),
                                   wm.self().vel(),
                                   wm.self().playerType().playerDecay() );
        // after inertia move, can stay in the target area
        if ( my_final.dist( move_point ) < dist_thr )
        {
            agent->debugClient().addMessage( "InertiaStay" );
            dlog.addText( Logger::TEAM,
                          __FILE__": doStopAtMovePoint. inertia stay" );
            return false;
        }

        // try to stop at the current point
        dlog.addText( Logger::TEAM,
                      __FILE__": doStopAtMovePoint. stop dash" );
        agent->debugClient().addMessage( "Stop" );
        agent->debugClient().setTarget( move_point );

        Body_StopDash( true ).execute( agent ); // save recovery
        agent->setNeckAction( new Neck_GoalieTurnNeck() );
        return true;
    }

    return false;
}

/*-------------------------------------------------------------------*/
/*!

 */
bool
Bhv_GoalieBasicMove::doMoveForDangerousState( PlayerAgent * agent,
                                              const Vector2D & move_point )
{
    const WorldModel& wm = agent->world();

    const double x_buf = 0.5;

    const Vector2D ball_next = wm.ball().pos() + wm.ball().vel();

    dlog.addText( Logger::TEAM,
                  __FILE__": doMoveForDangerousState" );

    if ( std::fabs( move_point.x - wm.self().pos().x ) > x_buf
         && ball_next.x < -ServerParam::i().pitchHalfLength() + 11.0
         && ball_next.absY() < ServerParam::i().goalHalfWidth() + 1.0 )
    {
        // x difference to the move point is over threshold
        // but ball is in very dangerous area (just front of our goal)

        // and, exist opponent close to ball
        if ( ! wm.opponentsFromBall().empty()
             && wm.opponentsFromBall().front()->distFromBall() < 2.0 )
        {
            Vector2D block_point
                = wm.opponentsFromBall().front()->pos();
            block_point.x -= 2.5;
            block_point.y = move_point.y;

            if ( wm.self().pos().x < block_point.x )
            {
                block_point.x = wm.self().pos().x;
            }

            dlog.addText( Logger::TEAM,
                          __FILE__": block opponent kickaer" );
            agent->debugClient().addMessage( "BlockOpp" );

            if ( doGoToMovePoint( agent, block_point ) )
            {
                return true;
            }

            double dist_thr = wm.ball().distFromSelf() * 0.1;
            if ( dist_thr < 0.5 ) dist_thr = 0.5;

            agent->debugClient().setTarget( block_point );
            agent->debugClient().addCircle( block_point, dist_thr );

            doGoToPointLookBall( agent,
                                 move_point,
                                 wm.ball().angleFromSelf(),
                                 dist_thr,
                                 ServerParam::i().maxDashPower() );
            return true;
        }
    }

    return false;
}

/*-------------------------------------------------------------------*/
/*!

 */
bool
Bhv_GoalieBasicMove::doCorrectX( PlayerAgent * agent,
                                 const Vector2D & move_point )
{
    const WorldModel & wm = agent->world();

    const double x_buf = 0.5;

    dlog.addText( Logger::TEAM,
                  __FILE__": doCorrectX" );
    if ( std::fabs( move_point.x - wm.self().pos().x ) < x_buf )
    {
        // x difference is already small.
        dlog.addText( Logger::TEAM,
                      __FILE__": doCorrectX. x diff is small" );
        return false;
    }

    int opp_min_cyc = wm.interceptTable().opponentStep();
    if ( ( ! wm.kickableOpponent() && opp_min_cyc >= 4 )
         || wm.ball().distFromSelf() > 18.0 )
    {
        double dash_power = getBasicDashPower( agent, move_point );

        dlog.addText( Logger::TEAM,
                      __FILE__": doCorrectX. power=%.1f",
                      dash_power );
        agent->debugClient().addMessage( "CorrectX%.0f", dash_power );
        agent->debugClient().setTarget( move_point );
        agent->debugClient().addCircle( move_point, x_buf );

        if ( ! wm.kickableOpponent()
             && wm.ball().distFromSelf() > 30.0 )
        {
            if ( ! Body_GoToPoint( move_point, x_buf, dash_power
                                   ).execute( agent ) )
            {
                AngleDeg body_angle = ( wm.self().body().degree() > 0.0
                                        ? 90.0
                                        : -90.0 );
                Body_TurnToAngle( body_angle ).execute( agent );

            }
            agent->setNeckAction( new Neck_TurnToBall() );
            return true;
        }

        doGoToPointLookBall( agent,
                             move_point,
                             wm.ball().angleFromSelf(),
                             x_buf,
                             dash_power );
        return true;
    }

    return false;
}

/*-------------------------------------------------------------------*/
/*!

 */
bool
Bhv_GoalieBasicMove::doCorrectBodyDir( PlayerAgent * agent,
                                       const Vector2D & move_point,
                                       const bool consider_opp )
{
    // adjust only body direction

    const WorldModel & wm = agent->world();

    const Vector2D ball_next = wm.ball().pos() + wm.ball().vel();

    const AngleDeg target_angle = ( ball_next.y < 0.0 ? -90.0 : 90.0 );
    const double angle_diff = ( wm.self().body() - target_angle ).abs();

    dlog.addText( Logger::TEAM,
                  __FILE__": doCorrectBodyDir" );

    if ( angle_diff < 5.0 )
    {
        return false;
    }

#if 1
    {
        const Vector2D goal_c( - ServerParam::i().pitchHalfLength(), 0.0 );
        Vector2D goal_to_ball = wm.ball().pos() - goal_c;
        if ( goal_to_ball.th().abs() >= 60.0 )
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": doCorrectBodyDir. danger area" );
            return false;
        }
    }
#else
    if ( wm.ball().pos().x < -36.0
         && wm.ball().pos().absY() < 15.0
         && wm.self().pos().dist( move_point ) > 1.5 )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": doCorrectBodyDir. danger area" );
        return false;
    }
#endif

    double opp_ball_dist
        = ( wm.opponentsFromBall().empty()
            ? 100.0
            : wm.opponentsFromBall().front()->distFromBall() );
    if ( ! consider_opp
         || opp_ball_dist > 7.0
         || wm.ball().distFromSelf() > 20.0
         || ( std::fabs( move_point.y - wm.self().pos().y ) < 1.0 // y diff
              && ! wm.kickableOpponent() ) )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": body face to %.1f.  angle_diff=%.1f %s",
                      target_angle.degree(), angle_diff,
                      consider_opp ? "consider_opp" : "" );
        agent->debugClient().addMessage( "CorrectBody%s",
                                         consider_opp ? "WithOpp" : "" );
        Body_TurnToAngle( target_angle ).execute( agent );
        agent->setNeckAction( new Neck_GoalieTurnNeck() );
        return true;
    }

    return false;
}

/*-------------------------------------------------------------------*/
/*!

 */
bool
Bhv_GoalieBasicMove::doGoToMovePoint( PlayerAgent * agent,
                                      const Vector2D & move_point )
{
    // move to target point
    // check Y coordinate difference

    const WorldModel & wm = agent->world();

    double dist_thr = wm.ball().distFromSelf() * 0.08;
    if ( dist_thr < 0.5 ) dist_thr = 0.5;

    const double y_diff = std::fabs( move_point.y - wm.self().pos().y );
    if ( y_diff < dist_thr )
    {
        // already there
        dlog.addText( Logger::TEAM,
                      __FILE__": doGoToMovePoint. y_diff=%.2f < thr=%.2f",
                      y_diff, dist_thr );
        return false;
    }

    //----------------------------------------------------------//
    // dash to body direction

    double dash_power = getBasicDashPower( agent, move_point );

    // body direction is OK
    if ( std::fabs( wm.self().body().abs() - 90.0 ) < 7.0 )
    {
        // calc dash power only to reach the target point
        double required_power = y_diff / wm.self().dashRate();
        if ( dash_power > required_power )
        {
            dash_power = required_power;
        }

        if ( move_point.y > wm.self().pos().y )
        {
            if ( wm.self().body().degree() < 0.0 )
            {
                dash_power *= -1.0;
            }
        }
        else
        {
            if ( wm.self().body().degree() > 0.0 )
            {
                dash_power *= -1.0;
            }
        }

        dash_power = ServerParam::i().normalizeDashPower( dash_power );

        dlog.addText( Logger::TEAM,
                      __FILE__": doGoToMovePoint. CorrectY(1) power= %.1f",
                      dash_power );
        agent->debugClient().addMessage( "CorrectY(1)%.0f", dash_power );
        agent->debugClient().setTarget( move_point );

        agent->doDash( dash_power );
        agent->setNeckAction( new Neck_GoalieTurnNeck() );
    }
    else
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": doGoToMovePoint. CorrectPos power= %.1f",
                      dash_power );
        agent->debugClient().addMessage( "CorrectPos%.0f", dash_power );
        agent->debugClient().setTarget( move_point );
        agent->debugClient().addCircle( move_point, dist_thr );

        doGoToPointLookBall( agent,
                             move_point,
                             wm.ball().angleFromSelf(),
                             dist_thr,
                             dash_power );
    }
    return true;
}

/*-------------------------------------------------------------------*/
/*!

 */
void
Bhv_GoalieBasicMove::doGoToPointLookBall( PlayerAgent * agent,
                                          const Vector2D & target_point,
                                          const AngleDeg & body_angle,
                                          const double & dist_thr,
                                          const double & dash_power,
                                          const double & back_power_rate )
{
    const WorldModel & wm = agent->world();

    if ( wm.gameMode().type() == GameMode::PlayOn
         || wm.gameMode().type() == GameMode::PenaltyTaken_ )
    {
        agent->debugClient().addMessage( "Goalie:GoToLook" );
        dlog.addText( Logger::TEAM,
                      __FILE__": doGoToPointLookBall. use GoToPointLookBall" );
        Bhv_GoToPointLookBall( target_point,
                               dist_thr,
                               dash_power,
                               back_power_rate
                               ).execute( agent );
    }
    else
    {
        agent->debugClient().addMessage( "Goalie:GoTo" );
        dlog.addText( Logger::TEAM,
                      __FILE__": doGoToPointLookBall. use GoToPoint" );
        if ( Body_GoToPoint( target_point, dist_thr, dash_power
                             ).execute( agent ) )
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": doGoToPointLookBall. go" );
        }
        else
        {
            Body_TurnToAngle( body_angle ).execute( agent );
            dlog.addText( Logger::TEAM,
                          __FILE__": doGoToPointLookBall. turn to %.1f",
                          body_angle.degree() );
        }

        agent->setNeckAction( new Neck_TurnToBall() );
    }
}
