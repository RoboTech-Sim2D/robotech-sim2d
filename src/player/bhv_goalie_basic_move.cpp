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
//
// PORTERO — POSICIONAMIENTO (reescrito limpio 2026-06-22).
//
// Filosofía (combina lo mejor de Cyrus + nuestra experiencia):
//   - El portero juega PEGADO a la línea (x ≈ -50.3) y se posiciona por
//     ÁNGULO: se planta en el punto que BISECTA el ángulo que el tirador ve
//     hacia los dos postes (método "MarliK 2011" de Cyrus). Así siempre tapa
//     el centro del ángulo y queda igual de cerca de ambos lados — clave
//     contra el centro atrás / cutback (el gol típico que encajábamos).
//   - NUNCA persigue desde aquí: la decisión de SALIR la toma
//     Bhv_GoalieChaseBall::is_ball_chase_situation (RoleGoalie::doMove), que
//     es conservadora (solo zona central y cercana, y solo si gana la carrera).
//
/////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "bhv_goalie_basic_move.h"

#include "bhv_basic_tackle.h"
#include "bhv_goalie_chase_ball.h"
#include "neck_goalie_turn_neck.h"

#include "basic_actions/basic_actions.h"
#include "basic_actions/body_go_to_point.h"

#include <rcsc/player/player_agent.h>
#include <rcsc/player/intercept_table.h>
#include <rcsc/player/debug_client.h>

#include <rcsc/common/logger.h>
#include <rcsc/common/server_param.h>
#include <rcsc/geom/line_2d.h>
#include <rcsc/soccer_math.h>

#include <algorithm>
#include <cmath>

using namespace rcsc;

/*-------------------------------------------------------------------*/
/*!
  Posicionar el portero. La salida (chase) se decide en otro sitio.
 */
bool
Bhv_GoalieBasicMove::execute( PlayerAgent * agent )
{
    const WorldModel & wm = agent->world();
    const ServerParam & SP = ServerParam::i();

    // PRIORITY 0: tackle si es posible (último recurso ante un balón al alcance).
    if ( Bhv_BasicTackle( 1.0, 98.0 ).execute( agent ) )
    {
        return true;
    }

    // ---------------------------------------------------------------
    // PRIORITY 0.5: REACCIÓN A TIRO (la atajada).
    //   Si el balón viaja hacia NUESTRA portería y su trayectoria cruza (cerca
    //   de) la boca, el portero se lanza YA al punto donde la trayectoria corta
    //   su línea, a máxima potencia. Sin esto el GK se quedaba quieto ante tiros
    //   directos. NO persigue balones wide: solo reacciona a los que van a la
    //   portería (|cross_y| < ancho de portería + margen).
    {
        const Vector2D bpos = wm.ball().pos();
        const Vector2D bvel = wm.ball().vel();
        const double goal_x = -SP.pitchHalfLength();

        if ( bvel.x < -0.5            // balón hacia nuestra meta (-x)
             && bvel.r() > 0.8        // con velocidad real (es un tiro, no un toque)
             && bpos.x < -20.0 )      // en nuestro campo
        {
            const double t_goal = ( goal_x - bpos.x ) / bvel.x;   // ciclos hasta la línea de gol
            // ¿el balón LLEGA de verdad a la portería con decaimiento? Si no,
            // NO reaccionar: antes el GK perseguía balones lentos lejanos (p.ej.
            // en la esquina) que nunca llegaban, y se descolocaba persiguiendo fantasmas.
            const double path_len   = ( bpos.x - goal_x ) * bvel.r() / ( -bvel.x );
            const double max_travel = bvel.r() / ( 1.0 - SP.ballDecay() );
            if ( t_goal > 0.0
                 && max_travel > path_len * 0.9 )
            {
                const double cross_y = bpos.y + bvel.y * t_goal;  // dónde cruza la línea de gol
                if ( std::fabs( cross_y ) < SP.goalHalfWidth() + 1.5 )
                {
                    // Punto de la trayectoria sobre la línea del portero.
                    const double base_x = goal_x + 2.2;
                    const double t_line = ( base_x - bpos.x ) / bvel.x;
                    double block_y = ( t_line > 0.0 )
                                     ? ( bpos.y + bvel.y * t_line )
                                     : cross_y;
                    // Permitir alcanzar un balón que pasa justo por el borde del
                    // poste (la portería mide 14m → no clavarse exacto al palo).
                    const double reach = SP.goalHalfWidth() + 1.0;
                    if ( block_y >  reach ) block_y =  reach;
                    if ( block_y < -reach ) block_y = -reach;

                    const Vector2D block_pt( base_x, block_y );
                    dlog.addText( Logger::TEAM,
                                  __FILE__": SAVE → (%.1f %.1f) cross_y=%.2f",
                                  block_pt.x, block_pt.y, cross_y );
                    agent->debugClient().addMessage( "GK_Save" );
                    agent->debugClient().setTarget( block_pt );

                    // PUNTO-BLANCO (port Cyrus isGoal→tackle): si el tiro entrante
                    // ya está al alcance del tackle, LANZARSE. Un tackle tiene
                    // alcance y es INSTANTÁNEO → desvía un disparo a quemarropa al
                    // que jamás llegaríamos corriendo (causa de "no va por el
                    // balón"). Umbral bajo (0.3): si va a ser gol igual, lanzarse
                    // siempre es mejor que quedarse quieto. Gateado a "tiro al
                    // arco" (estamos dentro del bloque de atajada) → seguro.
                    if ( Bhv_BasicTackle( 0.3, 90.0 ).execute( agent ) )
                    {
                        agent->debugClient().addMessage( "GK_Lunge" );
                        return true;
                    }

                    // Si el balón aún no está al alcance del tackle: alcance
                    // LATERAL (dash/bipedal/back-dash, sin girar primero) al punto
                    // de cruce sobre la línea — llega al poste lejano más rápido
                    // que Body_GoToPoint. Pone el cuello al balón.
                    Bhv_GoalieChaseBall::doGoToCatchPoint( agent, block_pt );
                    return true;
                }
            }
        }
    }

    // Posicionamiento por ángulo (bisector MarliK).
    const Vector2D move_point = getTargetPoint( agent );
    const double dist = wm.self().pos().dist( move_point );

    dlog.addText( Logger::TEAM,
                  __FILE__": GK target=(%.2f %.2f) dist=%.2f",
                  move_point.x, move_point.y, dist );

    // Si hay que moverse, a MÁXIMA potencia: el portero apenas se desplaza,
    // la stamina nunca es problema; ir lento lo hace ver "congelado".
    //
    // BUG 3 DEL PORTERO (2026-07-05, análisis del equipo): con deadband 0.4 y
    // Body_GoToPoint, el GK peleaba consigo mismo — GoToPoint GIRA el cuerpo
    // hacia el target MarliK (que se desliza cada ciclo) y el bloque de abajo
    // lo devuelve a ±90°: 14,080 turns vs 4,927 dashes en la competencia.
    // FIX: histéresis 0.7m (deltas menores no valen un giro) y los ajustes
    // CORTOS se hacen con dash OMNI manteniendo el cuerpo de costado; solo
    // los desplazamientos largos usan Body_GoToPoint.
    if ( dist > 0.7 )
    {
        agent->debugClient().addMessage( "GK_Pos" );
        agent->debugClient().setTarget( move_point );

        if ( dist < 2.5 )
        {
            AngleDeg rel_dir = ( move_point - wm.self().pos() ).th()
                - wm.self().body();
            const double power = std::min( SP.maxDashPower(), dist * 40.0 );
            agent->doDash( power, rel_dir );
            agent->setNeckAction( new Neck_GoalieTurnNeck() );
            return true;
        }

        if ( ! Body_GoToPoint( move_point, 0.3, SP.maxDashPower() ).execute( agent ) )
        {
            Body_TurnToBall().execute( agent );
        }
        agent->setNeckAction( new Neck_GoalieTurnNeck() );
        return true;
    }

    // Ya en posición: orientar SIEMPRE DE COSTADO (±90°). El portero debe poder
    // dashear lateralmente AL INSTANTE cuando llegue el tiro; si encara el balón
    // con el cuerpo, ante el disparo tiene que GIRAR primero y pierde el
    // desplazamiento (visto en log: el GK giraba —Turn+1/ciclo— en vez de seguir
    // el balón —Dash casi sin cambiar— y le entraban por el lado). El cuello
    // (Neck_GoalieTurnNeck) ve el balón INDEPENDIENTEMENTE del cuerpo, así que de
    // costado NO perdemos visión. Histéresis ±0.5 cerca del centro para no girar
    // 180° si el balón cruza y=0.
    {
        const Vector2D ball_next = wm.ball().pos() + wm.ball().vel();
        const double cur_body = wm.self().body().degree();
        AngleDeg body_angle;
        if ( ball_next.y < -0.5 )
            body_angle = -90.0;
        else if ( ball_next.y > 0.5 )
            body_angle = 90.0;
        else
            // centro muerto: mantener el costado actual (no girar 180°).
            body_angle = ( std::fabs( cur_body + 90.0 ) < std::fabs( cur_body - 90.0 )
                           ? -90.0 : 90.0 );
        Body_TurnToAngle( body_angle ).execute( agent );
        agent->setNeckAction( new Neck_GoalieTurnNeck() );
    }

    return true;
}

/*-------------------------------------------------------------------*/
/*!
  Punto objetivo del portero: línea pegada al arco + Y por bisector de ángulo.
 */
Vector2D
Bhv_GoalieBasicMove::getTargetPoint( PlayerAgent * agent )
{
    const ServerParam & SP = ServerParam::i();
    const WorldModel & wm = agent->world();

    const double goal_half_w  = SP.goalHalfWidth();    // ≈ 3.66
    const double pitch_half_l = SP.pitchHalfLength();  // 52.5
    const double goal_x       = -pitch_half_l;

    // 1) Predecir dónde estará el balón. Si nadie lo controla, su punto de
    //    intercepción más cercano (rival o compañero).
    int ball_reach_step = 0;
    if ( ! wm.kickableTeammate()
         && ! wm.kickableOpponent() )
    {
        ball_reach_step = std::min( wm.interceptTable().teammateStep(),
                                    wm.interceptTable().opponentStep() );
        // No sobre-predecir: si nadie va al balón el step puede ser enorme y el
        // punto de inercia se va lejos, descolocando al portero.
        ball_reach_step = std::min( ball_reach_step, 6 );
    }
    const Vector2D ball_pos = wm.ball().inertiaPoint( ball_reach_step );

    // PROFUNDIDAD DINÁMICA: con el balón CERCA, pegado a la línea (≈-50.3) para
    // cubrir el arco; con el balón LEJOS, ADELANTADO (hasta ≈-47.5) para achicar
    // el ángulo del arco gigante de 14m. Se repliega solo al acercarse el balón.
    double base_move_x = goal_x + 2.2;                 // ≈ -50.3 (balón cerca)
    if ( ball_pos.x > -45.0 )
    {
        const double t = std::min( 1.0, ( ball_pos.x + 45.0 ) / 30.0 );  // 0 en x=-45 → 1 en x=-15
        base_move_x = goal_x + 2.2 + t * 2.8;          // hasta ≈ -47.5
    }

    // ACHIQUE 1v1 (CONSERVADOR): salir a achicar el ángulo SOLO ante una amenaza
    // CERCANA, y SIN pasarse de la línea. En el log se vio que salir 9m con el
    // balón a 11-14m dejaba al GK VARADO: el rival le pasaba/cruzaba el balón por
    // el lado y entraba al arco vacío. Ahora: solo si el balón está a ≤12m de la
    // portería, factor menor y tope a ≈5m de la línea (puede seguir reaccionando
    // con un dash lateral a cualquiera de los dos postes).
    if ( wm.gameMode().type() == GameMode::PlayOn
         && wm.kickableOpponent()
         && ball_pos.x < goal_x + 12.0          // amenaza CERCA (≤12m), no a media cancha
         && ball_pos.absY() < 12.0 )
    {
        const double adv = goal_x + 2.2 + ( ball_pos.x - goal_x ) * 0.25;
        base_move_x = std::max( base_move_x, std::min( adv, goal_x + 5.0 ) );  // tope ≈ -47.5
        agent->debugClient().addMessage( "GK_Narrow" );
    }

    // 2) Balón pegado a la línea de fondo y ABIERTO (ángulo muy cerrado) →
    //    cubrir el POSTE CERCANO. El tiro al poste lejano desde ahí es casi
    //    imposible geométricamente, así que tapamos el cercano.
    if ( ball_pos.x < goal_x + 8.0
         && ball_pos.absY() > goal_half_w + 2.0 )
    {
        const double post_y = ( ball_pos.y > 0.0 )
                              ? ( goal_half_w - 0.1 )
                              : -( goal_half_w - 0.1 );
        agent->debugClient().addMessage( "GK_Post" );
        dlog.addText( Logger::TEAM,
                      __FILE__": getTarget near-post (%.1f %.1f)", base_move_x, post_y );
        return Vector2D( base_move_x, post_y );
    }

    // 3) POSICIONAMIENTO POR ÁNGULO (MarliK 2011). Trazamos dos líneas desde el
    //    balón a cada poste y vemos dónde cruzan la línea del portero; el GK se
    //    coloca en el PUNTO MEDIO de esos dos cruces = bisector del ángulo de
    //    tiro. Queda igual de cerca de ambos lados → cubre el centro atrás.
    const Line2D gk_line( Vector2D( base_move_x,  30.0 ),
                          Vector2D( base_move_x, -30.0 ) );
    const Line2D ball_to_top( ball_pos, Vector2D( goal_x,  goal_half_w ) );
    const Line2D ball_to_bot( ball_pos, Vector2D( goal_x, -goal_half_w ) );

    const Vector2D top_cross = gk_line.intersection( ball_to_top );
    const Vector2D bot_cross = gk_line.intersection( ball_to_bot );

    double move_y;
    if ( top_cross.isValid() && bot_cross.isValid() )
    {
        const double bisector = 0.5 * ( top_cross.y + bot_cross.y );
        // SESGO AL LADO DEL BALÓN (cubrir el POSTE CERCANO). Con un arco de 14m
        // y el GK pegado al fondo, el bisector PURO lo deja demasiado central:
        // en un tiro raso al palo cercano no llega (tiene que cubrir ~5m en pocos
        // ciclos). Sombreamos hacia el balón → cubrimos el palo cercano y
        // concedemos solo el tiro cruzado largo (lento, da tiempo a recuperar).
        const double ball_side = std::max( -goal_half_w,
                                  std::min( goal_half_w, ball_pos.y ) );
        // Menos sesgo al palo cercano (0.35) = más CENTRAL = mejor cobertura del
        // poste LEJANO (el que nos costaba goles). Ahora que el GK es rápido y
        // achica, alcanza el palo cercano sin necesitar tanto sesgo.
        move_y = 0.65 * bisector + 0.35 * ball_side;
    }
    else
    {
        move_y = ball_pos.y * 0.9;                       // fallback: seguimiento directo
    }

    // No salirse de la portería (cap al poste). Ante un balón muy abierto el GK
    // se queda en el poste, listo para el centro atrás — NUNCA se va a y=±13
    // como antes (eso costaba los goles de cutback).
    const double y_lim = goal_half_w - 0.1;
    if ( move_y >  y_lim ) move_y =  y_lim;
    if ( move_y < -y_lim ) move_y = -y_lim;

    dlog.addText( Logger::TEAM,
                  __FILE__": getTarget MarliK base_x=%.1f move_y=%.2f", base_move_x, move_y );
    return Vector2D( base_move_x, move_y );
}
