// -*-c++-*-

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "role_goalie.h"

#include "bhv_goalie_basic_move.h"
#include "bhv_goalie_chase_ball.h"
#include "bhv_goalie_free_kick.h"

#include "basic_actions/basic_actions.h"
#include "basic_actions/body_clear_ball.h"
#include "basic_actions/body_stop_dash.h"
#include "basic_actions/neck_scan_field.h"
#include "basic_actions/body_go_to_point.h"
#include "basic_actions/body_intercept.h"
#include "neck_goalie_turn_neck.h"

#include <rcsc/player/player_agent.h>
#include <rcsc/player/debug_client.h>
#include <rcsc/player/world_model.h>
#include <rcsc/player/intercept_table.h>

#include <rcsc/common/logger.h>
#include <rcsc/common/server_param.h>
#include <rcsc/common/audio_memory.h>
#include <rcsc/geom/rect_2d.h>

using namespace rcsc;

const std::string RoleGoalie::NAME( "Goalie" );

namespace {
    rcss::RegHolder role = SoccerRole::creators()
        .autoReg( &RoleGoalie::create, RoleGoalie::name() );
}

bool
RoleGoalie::execute( PlayerAgent * agent )
{
    const WorldModel & wm = agent->world();
    const ServerParam & SP = ServerParam::i();

    // ——————— 1. DEFINIMOS HOME Y CENTRO ———————
    static const Vector2D fieldCenter( 0.0, 0.0 );
    static const Vector2D goalieHome( -SP.pitchHalfLength() + 1.0, 0.0 );

    // ——————— 2. SI ES KICKOFF (pelota CENTRADA y PARADA) ———————
    if ( wm.ball().pos().dist( fieldCenter ) < 0.1
         && wm.ball().vel().norm() < 0.05
         && wm.self().pos().dist( goalieHome ) > 0.5 )
    {
        // Llévalo a la portería (home). Le pasamos todos los parámetros que pide el constructor:
        Body_GoToPoint goHome(
            goalieHome,       // punto destino
            0.5,              // radio de “llegada”
            1.0,              // margen para empezar a frenar
            SP.maxDashPower(),// potencia máxima
            30,               // ciclos extra para llegar
            true,             // permitir detenerse
            0.5,              // ganancia proporcional
            0.1,              // ganancia integral
            false             // urgente?
        );
        goHome.execute( agent );

        // siempre girar cuello mientras reposiciona
        agent->setNeckAction( new Neck_GoalieTurnNeck() );
        return true;
    }

    // ——————— 3. LUEGO VA TU LÓGICA NORMAL ———————

    // Atrapar si es catchable
    static const Rect2D our_penalty(
        Vector2D( -SP.pitchHalfLength(),
                  -SP.penaltyAreaHalfWidth() + 1.0 ),
        Size2D( SP.penaltyAreaLength() - 1.0,
                SP.penaltyAreaWidth()  - 2.0 ) );

    // ——— Regla de BACKPASS (Cyrus): el portero NO puede atrapar legalmente un
    // balón pateado deliberadamente por un compañero (sería falta → libre
    // indirecto dentro de nuestra área). Marcamos una ventana de "peligro" y,
    // dentro de ella, DESPEJAMOS en vez de atrapar. Un disparo rápido que ya
    // entra a portería sí se ataja (es un tiro real, no un backpass).
    static bool isDanger = false;
    static int  dangerCycle = 0;
    if ( wm.lastKickerSide() == wm.ourSide() )
    {
        isDanger = true;
        dangerCycle = wm.time().cycle();
    }
    if ( wm.audioMemory().passTime() == wm.time()
         && ! wm.audioMemory().pass().empty() )
    {
        isDanger = true;
        dangerCycle = wm.time().cycle();
    }
    if ( isDanger )
    {
        if ( wm.gameMode().type() != GameMode::PlayOn )       isDanger = false;
        if ( wm.time().cycle() - dangerCycle > 33
             && wm.ball().vel().r() > 1.0 )                   isDanger = false;
        if ( wm.kickableOpponent() || wm.kickableTeammate() ) isDanger = false;
    }

    if ( wm.time().cycle()
         > wm.self().catchTime().cycle() + SP.catchBanCycle()
         && wm.ball().distFromSelf() < SP.catchableArea() - 0.05
         // El balón debe estar ENFRENTE (Cyrus): no se puede atrapar de espaldas.
         && ( ( wm.ball().pos() - wm.self().pos() ).th() - wm.self().body() ).abs() < 90.0
         && our_penalty.contains( wm.ball().pos() ) )
    {
        // ¿tiro real entrando a portería? entonces atajamos aunque estemos en
        // la ventana de backpass (no es un backpass, es un disparo).
        const bool real_shot = ( wm.ball().vel().r() > 2.6
                                 && wm.ball().inertiaPoint( 5 ).x < -SP.pitchHalfLength() );
        if ( ! isDanger || real_shot )
        {
            isDanger = false;
            agent->doCatch();
            agent->setNeckAction( new Neck_TurnToBall() );
        }
        else
        {
            // backpass deliberado: atrapar sería falta → despejar.
            isDanger = false;
            dlog.addText( Logger::TEAM, __FILE__": backpass danger → clear instead of catch" );
            doKick( agent );
        }
    }
    else if ( wm.self().isKickable() )
    {
        doKick( agent );
    }
    else
    {
        doMove( agent );
    }

    return true;
}




void
RoleGoalie::doKick( PlayerAgent * agent )
{
    Body_ClearBall().execute( agent );
    agent->setNeckAction( new Neck_ScanField() );
}

void
RoleGoalie::doMove( PlayerAgent * agent )
{
    const WorldModel & wm = agent->world();
    const ServerParam & SP = ServerParam::i();

    if ( Bhv_GoalieChaseBall::is_ball_chase_situation( agent ) )
    {
        dlog.addText( Logger::TEAM, __FILE__": doMove -> chaseBall" );
        Bhv_GoalieChaseBall().execute( agent );
        return;
    }

    // Intercept completo (Cyrus): comprometerse a una intercepción que ganamos
    // CLARO (más rápido que los compañeros y 2+ pasos antes que el rival), pero
    // SOLO si el punto de corte cae dentro de nuestra área → nunca abandona el
    // arco persiguiendo un 50/50. Complementa el chase conservador (que solo
    // cubre el área chica): aquí extendemos a toda el área grande cuando es
    // claramente nuestro.
    const int self_min = wm.interceptTable().selfStep();
    const int mate_min = wm.interceptTable().teammateStep();
    const int opp_min  = wm.interceptTable().opponentStep();
    const Vector2D self_int = wm.ball().inertiaPoint( self_min );
    if ( wm.gameMode().type() == GameMode::PlayOn
         && self_min < mate_min
         && self_min < opp_min - 2
         && wm.ball().posCount() < 2
         && self_int.x < SP.ourPenaltyAreaLineX()
         && self_int.absY() < SP.penaltyAreaHalfWidth() )
    {
        if ( Body_Intercept( false ).execute( agent ) )
        {
            dlog.addText( Logger::TEAM, __FILE__": doMove -> full intercept (clear win)" );
            agent->debugClient().addMessage( "GKFullInt" );
            agent->setNeckAction( new Neck_TurnToBall() );
            return;
        }
    }

    dlog.addText( Logger::TEAM, __FILE__": doMove -> basicMove" );
    Bhv_GoalieBasicMove().execute( agent );
}
