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

    double ball_dist = wm.ball().distFromSelf();
    Vector2D ball_pos = wm.ball().pos();

    // NUEVO: Variables estáticas para histéresis (evitar bucle)
    static bool s_was_pressing = false;
    static GameTime s_last_press_time(0, 0);

    // NUEVO: En saques de banda/córner, mantener posición conservadora
    if ( wm.gameMode().type() == GameMode::KickIn_
         || wm.gameMode().type() == GameMode::CornerKick_ )
    {
        Vector2D conservative_pos(-49.0, 0.0);
        std::cerr << "[DBG Bhv_GoalieBasicMove] saque de banda/córner, posición conservadora" 
                  << std::endl;
        agent->debugClient().addMessage( "ConservativePos" );
        agent->doTurn( 0.0 );
        agent->setNeckAction( new Neck_GoalieTurnNeck() );
        return Body_GoToPoint( conservative_pos, 1.0, SP.maxDashPower() * 0.5 ).execute( agent );
    }

    // 1) SI EL BALÓN ESTÁ MUY CERCA DE SU ÁREA → Volver a home (CON HISTÉRESIS)
    const double CLOSE_DIST = 15.0;
    const double CLOSE_DIST_HYSTERESIS = 18.0;  // Umbral más alto si estaba presionando
    const Vector2D HOME_UP   = Vector2D(-51.0,  6.0);
    const Vector2D HOME_DOWN = Vector2D(-51.0, -6.0);
    const Vector2D HOME_POINT = (ball_pos.y >= 0.0 ? HOME_UP : HOME_DOWN);

    double close_threshold = s_was_pressing ? CLOSE_DIST_HYSTERESIS : CLOSE_DIST;

    if ( ball_dist < close_threshold )
    {
        s_was_pressing = false;  // Ya no está presionando
        std::cerr << "[DBG Bhv_GoalieBasicMove] balón cerca (" << ball_dist 
                  << "m), volviendo a home: " << HOME_POINT << std::endl;
        agent->doTurn( 0.0 );
        agent->setNeckAction( new Neck_GoalieTurnNeck() );
        return Body_GoToPoint( HOME_POINT, 1.0, SP.maxDashPower() ).execute( agent );
    }

    // 2) SI HAY RIVAL MUY CERCA SIN APOYO → Salgo a presionar (CON HISTÉRESIS Y VERIFICACIÓN DE APOYO)
    {
        Vector2D area_center( SP.ourPenaltyAreaLineX() + SP.penaltyAreaLength()/2.0, 0.0 );
        double opp_dist = 1e6;
        for ( auto * opp : wm.opponentsFromBall() )
            opp_dist = std::min( opp_dist, opp->pos().dist( area_center ) );

        int self_step = wm.interceptTable().selfStep();
        int tm_step   = wm.interceptTable().teammateStep();
        int opp_step  = wm.interceptTable().opponentStep();

        const double PRESS_DIST = 7.0;
        const double PRESS_DIST_HYSTERESIS = 5.0;

        // NUEVO: Verificar si hay compañero con mejor posición
        bool teammate_has_better_position = false;
        if ( wm.kickableTeammate() )
        {
            teammate_has_better_position = true;
            std::cerr << "[DBG Bhv_GoalieBasicMove] compañero tiene el balón, no presiono" << std::endl;
        }
        else if ( tm_step < self_step - 1 )  // Compañero llega mucho antes
        {
            teammate_has_better_position = true;
            std::cerr << "[DBG Bhv_GoalieBasicMove] compañero llega antes (tm:" << tm_step 
                      << " vs self:" << self_step << "), no presiono" << std::endl;
        }

        // Si estaba presionando pero ahora hay apoyo, dejar de presionar inmediatamente
        if ( s_was_pressing && teammate_has_better_position )
        {
            s_was_pressing = false;
            std::cerr << "[DBG Bhv_GoalieBasicMove] llegó apoyo, dejo de presionar y vuelvo" << std::endl;
            // No hacer return aquí, dejar que continúe con la lógica normal de posicionamiento
        }

        // Usar histéresis: si NO estaba presionando, necesita oponente MÁS cerca
        double press_threshold = s_was_pressing ? PRESS_DIST : PRESS_DIST_HYSTERESIS;

        if ( opp_dist < press_threshold
             && ! teammate_has_better_position
             && ball_dist >= close_threshold )  // Solo si NO está en zona "volver a home"
        {
            s_was_pressing = true;
            s_last_press_time = wm.time();
            std::cerr << "[DBG Bhv_GoalieBasicMove] salgo a presionar rival cerca (" 
                      << opp_dist << "m) sin apoyo" << std::endl;
            return Bhv_GoalieChaseBall().execute( agent );
        }
        else if ( s_was_pressing && opp_dist >= PRESS_DIST )
        {
            s_was_pressing = false;
        }
    }


    // 3) ELIMINADO: La condición de "balón lejano" era demasiado agresiva
    // El portero perseguía balones a 60-90m incluso en campo contrario
    // Esto dejaba la portería desprotegida en contraataques
    // Las otras condiciones (presionar rival cerca, zona peligro) son suficientes

    // ── Resto de tu lógica sin cambios ──
    const Vector2D move_point = getTargetPoint( agent );
    dlog.addText( Logger::TEAM,
                  __FILE__": Bhv_GoalieBasicMove. move_point(%.2f %.2f)",
                  move_point.x, move_point.y );

    if ( Bhv_BasicTackle( 1.0, 98.0 ).execute( agent ) ) return true;
    if ( doPrepareDeepCross   ( agent, move_point ) )      return true;
    if ( doStopAtMovePoint    ( agent, move_point ) )      return true;
    if ( doMoveForDangerousState( agent, move_point ) )    return true;
    if ( doCorrectX           ( agent, move_point ) )      return true;
    if ( doCorrectBodyDir     ( agent, move_point, true ) )  return true;
    if ( doGoToMovePoint      ( agent, move_point ) )      return true;
    if ( doCorrectBodyDir     ( agent, move_point, false ) ) return true;

    dlog.addText( Logger::TEAM, __FILE__": only look ball" );
    agent->debugClient().addMessage( "OnlyTurnNeck" );
    agent->doTurn( 0.0 );
    agent->setNeckAction( new Neck_GoalieTurnNeck() );
    return true;
}



/*-------------------------------------------------------------------*/
/*!

 */
Vector2D
Bhv_GoalieBasicMove::getTargetPoint( PlayerAgent * agent )
{
    const double base_move_x = -43.0;
    const double danger_move_x = -49.0;
    const WorldModel & wm = agent->world();

    int ball_reach_step = 0;
    if ( ! wm.kickableTeammate()
         && ! wm.kickableOpponent() )
    {
        ball_reach_step
            = std::min( wm.interceptTable().teammateStep(),
                        wm.interceptTable().opponentStep() );
    }
    const Vector2D base_pos = wm.ball().inertiaPoint( ball_reach_step );


    //---------------------------------------------------------//
    // angle is very dangerous
    if ( base_pos.y > ServerParam::i().goalHalfWidth() + 5.0 )
    {
        Vector2D right_pole( - ServerParam::i().pitchHalfLength(),
                             ServerParam::i().goalHalfWidth() );
        AngleDeg angle_to_pole = ( right_pole - base_pos ).th();

        if ( -140.0 < angle_to_pole.degree()
             && angle_to_pole.degree() < -90.0 )
        {
            agent->debugClient().addMessage( "RPole" );
            return Vector2D( danger_move_x, ServerParam::i().goalHalfWidth() + 0.001 );
        }
    }
    else if ( base_pos.y < -ServerParam::i().goalHalfWidth() - 3.0 )
    {
        Vector2D left_pole( - ServerParam::i().pitchHalfLength(),
                            - ServerParam::i().goalHalfWidth() );
        AngleDeg angle_to_pole = ( left_pole - base_pos ).th();

        if ( 90.0 < angle_to_pole.degree()
             && angle_to_pole.degree() < 145.0 )
        {
            agent->debugClient().addMessage( "LPole" );
            return Vector2D( danger_move_x, - ServerParam::i().goalHalfWidth() - 0.001 );
        }
    }

    //---------------------------------------------------------//
    // ball is close to goal line
    if ( base_pos.x < -ServerParam::i().pitchHalfLength() + 9.0
         && base_pos.absY() > ServerParam::i().goalHalfWidth() + 2.8 )
    {
        Vector2D target_point( base_move_x, ServerParam::i().goalHalfWidth() - 0.4 );
        if ( base_pos.y < 0.0 )
        {
            target_point.y *= -1.0;
        }

        dlog.addText( Logger::TEAM,
                      __FILE__": getTarget. target is goal pole" );
        agent->debugClient().addMessage( "Pos(1)" );

        return target_point;
    }

//---------------------------------------------------------//
    {
        const double x_back = 4.0; // tune this!!
        int ball_pred_cycle = 8; // tune this!!
        const double y_buf = 0.5; // tune this!!
        const Vector2D base_point( - ServerParam::i().pitchHalfLength() - x_back,
                                   0.0 );
        Vector2D ball_point;
        if ( wm.kickableOpponent() )
        {
            ball_point = base_pos;
            agent->debugClient().addMessage( "Pos(2)" );
        }
        else
        {
            int opp_min = wm.interceptTable().opponentStep();
            if ( opp_min < ball_pred_cycle )
            {
                ball_pred_cycle = opp_min;
                dlog.addText( Logger::TEAM,
                              __FILE__": opp may reach near future. cycle = %d",
                              opp_min );
            }

            ball_point
                = inertia_n_step_point( base_pos,
                                        wm.ball().vel(),
                                        ball_pred_cycle,
                                        ServerParam::i().ballDecay() );
            agent->debugClient().addMessage( "Pos(3)" );
        }

        if ( ball_point.x < base_point.x + 0.1 )
        {
            ball_point.x = base_point.x + 0.1;
        }

        Line2D ball_line( ball_point, base_point );
        double move_y = ball_line.getY( base_move_x );

        if ( move_y > ServerParam::i().goalHalfWidth() - y_buf )
        {
            move_y = ServerParam::i().goalHalfWidth() - y_buf;
        }
        if ( move_y < - ServerParam::i().goalHalfWidth() + y_buf )
        {
            move_y = - ServerParam::i().goalHalfWidth() + y_buf;
        }

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

    if ( wm.ball().pos().x > -30.0 )
    {
        if ( wm.self().stamina() < ServerParam::i().staminaMax() * 0.9 )
        {
            return my_inc * 0.5;
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
