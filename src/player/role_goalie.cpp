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
#include "neck_goalie_turn_neck.h"

#include <rcsc/player/player_agent.h>
#include <rcsc/player/debug_client.h>
#include <rcsc/player/world_model.h>

#include <rcsc/common/logger.h>
#include <rcsc/common/server_param.h>
#include <rcsc/geom/rect_2d.h>

#include <iostream>
#include <sstream>

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

    if ( wm.time().cycle()
         > wm.self().catchTime().cycle() + SP.catchBanCycle()
         && wm.ball().distFromSelf() < SP.catchableArea() - 0.05
         && our_penalty.contains( wm.ball().pos() ) )
    {
        agent->doCatch();
        agent->setNeckAction( new Neck_TurnToBall() );
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
    GameMode::Type gm = agent->world().gameMode().type();
    std::cerr << "[DBG RoleGoalie::doMove] gm(enum)=" << gm << std::endl;

    if ( Bhv_GoalieChaseBall::is_ball_chase_situation( agent ) )
    {
        std::cerr << "  -> chaseBall" << std::endl;
        Bhv_GoalieChaseBall().execute( agent );
    }
    else
    {
        std::cerr << "  -> basicMove" << std::endl;
        Bhv_GoalieBasicMove().execute( agent );
    }
}
