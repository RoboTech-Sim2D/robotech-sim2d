// -*-c++-*-
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "bhv_mark_opponent.h"
#include "strategy.h"

#include "basic_actions/body_go_to_point.h"
#include "basic_actions/arm_point_to_point.h"
#include "basic_actions/neck_turn_to_ball_or_scan.h"
#include "basic_actions/basic_actions.h"

#include <rcsc/player/player_agent.h>
#include <rcsc/player/world_model.h>
#include <rcsc/common/server_param.h>
#include <rcsc/common/logger.h>

#include <algorithm>

using namespace rcsc;

bool
Bhv_MarkOpponent::execute( PlayerAgent * agent )
{
    const WorldModel & wm = agent->world();
    const ServerParam & SP = ServerParam::i();

    // Solo actúa si el balón está en nuestra mitad y no hay compañero con el balón
    if ( wm.ball().pos().x > 5.0 ) return false;
    if ( wm.kickableTeammate() )    return false;

    const int role = Strategy::i().roleNumber( wm.self().unum() );

    // Solo para defensores de campo (roles 2-5)
    if ( role < 2 || role > 5 ) return false;

    const Vector2D goal_center( -SP.pitchHalfLength(), 0.0 );

    // ── Buscar el rival más peligroso sin marcar ──────────────────────────
    const PlayerObject * mark_target = nullptr;
    double best_score = -1.0e9;

    for ( const PlayerObject * opp : wm.opponents() )
    {
        if ( ! opp )             continue;
        if ( opp->isGhost() )    continue;
        if ( opp->goalie() )     continue;
        if ( opp->pos().x > 5.0 ) continue;  // solo en nuestra mitad

        // ── Anti-doble-marcaje: ¿hay un compañero defensor más cercano? ──
        bool claimed = false;
        for ( const PlayerObject * tm : wm.teammates() )
        {
            if ( ! tm ) continue;
            if ( tm->unum() == wm.self().unum() ) continue;

            const int tm_role = Strategy::i().roleNumber( tm->unum() );
            if ( tm_role < 2 || tm_role > 5 ) continue;  // solo defensores

            const double tm_dist   = tm->pos().dist( opp->pos() );
            const double self_dist = wm.self().pos().dist( opp->pos() );

            // Si el compañero está claramente más cerca, el rival ya está cubierto
            if ( tm_dist < self_dist - 1.5 )
            {
                claimed = true;
                break;
            }
        }
        if ( claimed ) continue;

        // ── Puntuación de peligro: más peligroso cuanto más cerca a nuestra portería ──
        double score = -opp->pos().dist( goal_center )       // cercanía a portería
                       - opp->pos().absY() * 0.2;            // penalizar posición muy lateral

        if ( score > best_score )
        {
            best_score  = score;
            mark_target = opp;
        }
    }

    if ( ! mark_target ) return false;

    const Vector2D opp_pos = mark_target->pos();

    // ── Posición de corte de pase: sobre la línea balón→rival ────────────
    // Proyectamos la posición del balón al momento en que el rival lo recibiría
    const int opp_reach = wm.interceptTable().opponentStep();
    const Vector2D ball_pos = wm.ball().inertiaPoint( opp_reach );
    const double ball_opp_dist = opp_pos.dist( ball_pos );

    // Buscamos el punto de la línea balón→rival más cercano a nosotros
    // (dentro de 12m del rival y sin sobrepasar al balón)
    const AngleDeg ball_dir = ( ball_pos - opp_pos ).th();
    Vector2D pass_cut_pos = opp_pos + Vector2D::polar2vector( 1.0, ball_dir );
    double   min_self_dist = wm.self().pos().dist( pass_cut_pos );

    for ( double d = 2.0; d < std::min( ball_opp_dist, 12.0 ); d += 1.0 )
    {
        Vector2D cand = opp_pos + Vector2D::polar2vector( d, ball_dir );
        const double dist = wm.self().pos().dist( cand );
        if ( dist < min_self_dist )
        {
            min_self_dist = dist;
            pass_cut_pos  = cand;
        }
    }

    // ── Posición lado-portería (fallback para zona de peligro) ───────────
    Vector2D opp_to_goal = goal_center - opp_pos;
    if ( opp_to_goal.r() > 0.001 )
        opp_to_goal.setLength( 1.8 );
    const Vector2D goal_side_pos = opp_pos + opp_to_goal;

    // ── Blend: corte de pase lejos, lado-portería cerca ──────────────────
    // blend=1 → 100% corte de pase (rival lejos de portería)
    // blend=0 → 100% lado portería (rival en zona peligrosa, dist < 5m)
    const double opp_goal_dist = opp_pos.dist( goal_center );
    const double blend = std::min( 1.0, std::max( 0.0,
                             ( opp_goal_dist - 5.0 ) / 15.0 ) );

    Vector2D mark_pos = pass_cut_pos * blend + goal_side_pos * ( 1.0 - blend );

    // ── Compresión lateral: acercarse al Y del balón para cubrir pases al lado
    const double y_diff = ball_pos.y - mark_pos.y;
    mark_pos.y += y_diff * 0.3;

    // Limitar para no entrar demasiado en la portería
    mark_pos.x = std::max( mark_pos.x, -SP.pitchHalfLength() + 2.0 );

    dlog.addText( Logger::TEAM,
                  __FILE__": MarkOpponent unum=%d pos=(%.1f %.1f) mark_pos=(%.1f %.1f)",
                  mark_target->unum(),
                  mark_target->pos().x, mark_target->pos().y,
                  mark_pos.x, mark_pos.y );

    agent->debugClient().addMessage( "Mark%d", mark_target->unum() );
    agent->debugClient().setTarget( mark_pos );

    const double dist_thr = 0.8;
    if ( wm.self().pos().dist( mark_pos ) < dist_thr )
        return false;  // ya está en posición, dejar que el flujo normal gire hacia el balón

    Body_GoToPoint( mark_pos, dist_thr, SP.maxDashPower() ).execute( agent );

    if ( wm.ball().distFromSelf() < 15.0 )
        agent->setNeckAction( new Neck_TurnToBall() );
    else
        agent->setNeckAction( new Neck_TurnToBallOrScan( 0 ) );

    return true;
}
