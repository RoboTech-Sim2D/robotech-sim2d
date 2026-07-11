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
#include "bhv_basic_block.h"
#include "cyrus_interceptable.h"
#include "basic_actions/body_intercept_plan.h"

#include "basic_actions/body_go_to_point.h"
#include "basic_actions/body_turn_to_point.h"
#include "basic_actions/neck_turn_to_ball_or_scan.h"
#include "basic_actions/neck_turn_to_ball.h"

#include <rcsc/player/player_agent.h>
#include <rcsc/player/world_model.h>
#include <rcsc/player/intercept_table.h>
#include <rcsc/common/server_param.h>
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

    // ── PRESS/INTERCEPT before marking (ROOT-CAUSE FIX del 59% de carreras) ──
    // Cyrus corre Body_InterceptPlan ANTES del split defensa/ataque: el jugador
    // más rápido SIEMPRE disputa el balón. Nosotros saltábamos directo a marcar
    // aquí y NUNCA íbamos por el balón → llegábamos segundos / defensa pasiva.
    // Bhv_BasicBlock ya encapsula: "más rápido del equipo → Body_Intercept; si
    // no, bloqueador designado → tapa el carril; si no → return false". Correrlo
    // primero hace que EXACTAMENTE UN defensa presione el balón y el resto caiga
    // al marcaje. Es autolimitante (no genera racimo).
    // Pieza rescatada del port completo (2026-07-06, cambio único de tanda):
    // Body_InterceptPlan de Cyrus — la decisión de DISPUTA del balón con
    // CyrusPlayerIntercept + tackle-intercept. SOLO en la ruta defensiva; la
    // ofensiva no se toca (la succión de atacantes fue lo que mató el GF en
    // el trasplante completo). Si no dispara, sigue nuestro flujo normal.
    if ( Body_InterceptPlan().execute( agent ) ) {
        agent->debugClient().addMessage( "DefInterceptPlan" );
        return true;
    }

    if ( Bhv_BasicBlock().execute( agent ) ) {
        agent->debugClient().addMessage( "DefPress" );
        return true;
    }

    if ( wm.interceptTable().firstOpponent() == nullptr
      || wm.interceptTable().firstOpponent()->unum() < 1 ) {
        return false;
    }

    // ── PRESS al portador que CIRCULA (2026-07-03) ──
    // Con las formaciones ya sanas, los goles encajados dejaron de ser
    // penetraciones rápidas: son posesiones de 60-120 ciclos en nuestra mitad
    // con el back más cercano a ~6.6 m del balón. El bloqueo de arriba
    // intercepta trayectorias de REGATE, así que al rival que solo pasa el
    // balón nadie lo molesta. Aquí el jugador de campo MÁS CERCANO al balón
    // (uno solo, radio corto) sale a presionarlo por el lado de nuestra
    // portería; la línea no se mueve porque el resto sigue en marca/hueco.
    {
        const AbstractPlayerObject * carrier = wm.interceptTable().firstOpponent();
        const Vector2D ball_pos = wm.ball().pos();
        const double my_dist = wm.self().pos().dist( ball_pos );

        if ( ( wm.kickableOpponent() || opp_min <= 1 )
             && ball_pos.x < -15.0
             && my_dist < 8.0
             && wm.self().stamina() > ServerParam::i().recoverDecThrValue() + 600.0 )
        {
            bool i_am_nearest     = true;
            bool someone_pressing = false;
            for ( const PlayerObject * tm : wm.teammatesFromBall() )
            {
                if ( ! tm || tm->unum() < 1 || tm->goalie() ) continue;
                const double d = tm->pos().dist( ball_pos );
                if ( d < 2.5 ) { someone_pressing = true; }
                else if ( d < my_dist - 0.3 ) { i_am_nearest = false; }
                break;   // solo el compañero (de campo) más cercano importa
            }

            if ( i_am_nearest && ! someone_pressing )
            {
                // FASE 1 (2026-07-03): presionar el punto de TRAP predicho
                // (CyrusPlayerIntercept) — con el pase en viaje llega al
                // punto de recepción, no a donde está parado el receptor.
                Vector2D press_base = carrier->pos();
                Vector2D trap_pos;
                if ( CyrusPlayerIntercept::opponentTrap( wm, &trap_pos, nullptr )
                     && trap_pos.isValid()
                     && trap_pos.dist( press_base ) < 15.0 ) {
                    press_base = trap_pos;
                }

                Vector2D our_goal( -ServerParam::i().pitchHalfLength(), 0.0 );
                Vector2D press_pt = press_base
                    + ( our_goal - press_base ).setLengthVector( 1.5 );

                agent->debugClient().addMessage( "CarrierPress" );
                agent->debugClient().setTarget( press_pt );
                if ( ! Body_GoToPoint( press_pt, 0.4, 100.0 ).execute( agent ) ) {
                    Body_TurnToPoint( ball_pos ).execute( agent );
                }
                agent->setNeckAction( new Neck_TurnToBall() );
                return true;
            }
        }
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
