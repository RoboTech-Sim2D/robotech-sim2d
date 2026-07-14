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

#include "sample_player.h"

#include "strategy.h"
#include "field_analyzer.h"
#include "localization_denoiser_by_area.h"
#include "localization_denoiser_by_action.h"

#include "action_chain_holder.h"
#include "sample_field_evaluator.h"

#include "soccer_role.h"

#include "sample_communication.h"
#include "keepaway_communication.h"
#include "sample_freeform_message_parser.h"

#include "bhv_penalty_kick.h"
#include "bhv_set_play.h"
#include "bhv_set_play_kick_in.h"
#include "bhv_set_play_indirect_free_kick.h"

#include "bhv_custom_before_kick_off.h"
#include "bhv_strict_check_shoot.h"

#include "view_tactical.h"

#include "intention_receive.h"

#include "basic_actions/basic_actions.h"
#include "basic_actions/bhv_emergency.h"
#include "basic_actions/body_go_to_point.h"
#include "basic_actions/body_intercept.h"
#include "basic_actions/body_kick_one_step.h"
#include "basic_actions/neck_scan_field.h"
#include "basic_actions/neck_turn_to_ball_or_scan.h"
#include "basic_actions/view_synch.h"
#include "basic_actions/kick_table.h"

#include <rcsc/formation/formation.h>
#include <rcsc/player/intercept_table.h>
#include <rcsc/player/say_message_builder.h>
#include <rcsc/player/audio_sensor.h>

#include <rcsc/common/abstract_client.h>
#include <rcsc/common/logger.h>
#include <rcsc/common/server_param.h>
#include <rcsc/common/player_param.h>
#include <rcsc/common/audio_memory.h>
#include <rcsc/common/say_message_parser.h>

#include <rcsc/param/param_map.h>
#include <rcsc/param/cmd_line_parser.h>

#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <chrono>    // PROFILING TEMPORAL O5
#include <fstream>   // PROFILING TEMPORAL O5

using namespace rcsc;

/*-------------------------------------------------------------------*/
/*!

 */
int SamplePlayer::player_port = 0;

SamplePlayer::SamplePlayer()
    : PlayerAgent(),
      M_communication()
{
    // Denoiser de localización (port Cyrus 2026-07-11): variante ByArea.
    // ByAction se PROBÓ Y FALLÓ su gate (tanda 2026-07-12: GF combinado
    // 0.29 vs 0.71 de ByArea; RoboCIn 1.0→0.43 GF y GA 2.7→3.3) — el modelo
    // "rival racional" estima peor que la factibilidad física pura a este
    // nivel de rivales. NO reintentar sin evidencia nueva.
    M_localization_denoiser = new LocalizationDenoiserByArea();
    M_field_evaluator = createFieldEvaluator();
    M_action_generator = createActionGenerator();

    std::shared_ptr< AudioMemory > audio_memory( new AudioMemory );

    M_worldmodel.setAudioMemory( audio_memory );

    //
    // set communication message parser
    //
    addSayMessageParser( new BallMessageParser( audio_memory ) );
    addSayMessageParser( new PassMessageParser( audio_memory ) );
    addSayMessageParser( new InterceptMessageParser( audio_memory ) );
    addSayMessageParser( new GoalieMessageParser( audio_memory ) );
    addSayMessageParser( new GoalieAndPlayerMessageParser( audio_memory ) );
    addSayMessageParser( new OffsideLineMessageParser( audio_memory ) );
    addSayMessageParser( new DefenseLineMessageParser( audio_memory ) );
    addSayMessageParser( new WaitRequestMessageParser( audio_memory ) );
    addSayMessageParser( new PassRequestMessageParser( audio_memory ) );
    addSayMessageParser( new DribbleMessageParser( audio_memory ) );
    addSayMessageParser( new BallGoalieMessageParser( audio_memory ) );
    addSayMessageParser( new OnePlayerMessageParser( audio_memory ) );
    addSayMessageParser( new TwoPlayerMessageParser( audio_memory ) );
    addSayMessageParser( new ThreePlayerMessageParser( audio_memory ) );
    addSayMessageParser( new SelfMessageParser( audio_memory ) );
    addSayMessageParser( new TeammateMessageParser( audio_memory ) );
    addSayMessageParser( new OpponentMessageParser( audio_memory ) );
    addSayMessageParser( new BallPlayerMessageParser( audio_memory ) );
    addSayMessageParser( new StaminaMessageParser( audio_memory ) );
    addSayMessageParser( new RecoveryMessageParser( audio_memory ) );

    // addSayMessageParser( new FreeMessageParser< 9 >( audio_memory ) );
    // addSayMessageParser( new FreeMessageParser< 8 >( audio_memory ) );
    // addSayMessageParser( new FreeMessageParser< 7 >( audio_memory ) );
    // addSayMessageParser( new FreeMessageParser< 6 >( audio_memory ) );
    // addSayMessageParser( new FreeMessageParser< 5 >( audio_memory ) );
    // addSayMessageParser( new FreeMessageParser< 4 >( audio_memory ) );
    // addSayMessageParser( new FreeMessageParser< 3 >( audio_memory ) );
    // addSayMessageParser( new FreeMessageParser< 2 >( audio_memory ) );
    // addSayMessageParser( new FreeMessageParser< 1 >( audio_memory ) );

    //
    // set freeform message parser
    //
    addFreeformMessageParser( new OpponentPlayerTypeMessageParser( M_worldmodel ) );

    //
    // set communication planner
    //
    M_communication = Communication::Ptr( new SampleCommunication() );
}

/*-------------------------------------------------------------------*/
/*!

 */
SamplePlayer::~SamplePlayer()
{

}

/*-------------------------------------------------------------------*/
/*!

 */
bool
SamplePlayer::initImpl( CmdLineParser & cmd_parser )
{
    bool result = PlayerAgent::initImpl( cmd_parser );

    // read additional options
    result &= Strategy::instance().init( cmd_parser );

    rcsc::ParamMap my_params( "Additional options" );
#if 0
    std::string param_file_path = "params";
    param_map.add()
        ( "param-file", "", &param_file_path, "specified parameter file" );
#endif

    cmd_parser.parse( my_params );

    if ( cmd_parser.count( "help" ) > 0 )
    {
        my_params.printHelp( std::cout );
        return false;
    }

    if ( cmd_parser.failed() )
    {
        std::cerr << "player: ***WARNING*** detected unsuppprted options: ";
        cmd_parser.print( std::cerr );
        std::cerr << std::endl;
    }

    if ( ! result )
    {
        return false;
    }

    if ( ! Strategy::instance().read( config().configDir() ) )
    {
        std::cerr << "***ERROR*** Failed to read team strategy." << std::endl;
        return false;
    }

    if ( KickTable::instance().read( config().configDir() + "/kick-table" ) )
    {
        std::cerr << "Loaded the kick table: ["
                  << config().configDir() << "/kick-table]"
                  << std::endl;
    }

    return true;
}

/*-------------------------------------------------------------------*/
/*!
  main decision
  virtual method in super class
*/
void
SamplePlayer::actionImpl()
{
    // VIGÍA DE RENDIMIENTO (permanente, costo ~0): registra en
    // ./slow_actions.csv (CWD del lanzamiento) cada decisión que tarde >70ms
    // (dorsal,ciclo,ms). Un ciclo >70ms suele costar la acción ("lost kick" =
    // jugadores que se quedan pensando). Ya cazó 2 regresiones reales:
    // eval-limit 320 (jul-05) y scape_voronoi sin escalonar (jul-11).
    // Leerlo tras cada tanda: eventos en ciclos <20 o fronteras (3000/6000)
    // son arranque/descanso e ignorables.
    struct ProfileGuard {
        std::chrono::steady_clock::time_point t0;
        const rcsc::WorldModel & wm;
        explicit ProfileGuard( const rcsc::WorldModel & w )
            : t0( std::chrono::steady_clock::now() ), wm( w ) { }
        ~ProfileGuard() {
            const double ms = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0 ).count() / 1000.0;
            if ( ms > 70.0 ) {
                std::ofstream f( "slow_actions.csv", std::ios::app );
                f << wm.self().unum() << ',' << wm.time().cycle()
                  << ',' << ms << '\n';
            }
        }
    } profile_guard_( world() );

    SamplePlayer::player_port = this->config().port();

    // Denoiser (port Cyrus 2026-07-11): corregir posiciones en el WM ANTES de
    // cualquier decisión. En Cyrus esto corre vía hook virtual de su fork;
    // aquí el inicio de actionImpl es el mismo punto efectivo (WM ya
    // actualizado, ninguna decisión tomada aún).
    update_player_by_denoiser();

    // AUDITORÍA DE RUIDO RESIDUAL (temporal; track-de-ruido paso 1, 2026-07-12).
    // Solo se activa corriendo con `--fullstate reference` (debug_fullstate):
    // el agente mantiene el WM ruidoso QUE DECIDE y el fullstate de verdad.
    // Cada 10 ciclos, cada jugador escribe UNA fila con el error |WM−verdad|
    // por objeto — DESPUÉS del denoiser, para medir lo que él NO corrige:
    // ciclo,unum,ball_perr,ball_verr,self_err,opp<10m,opp10-25,opp>25,mate_err
    // Archivo: ./noise_audit.csv (CWD del lanzamiento). Costo cero sin el flag.
    if ( config().debugFullstate()
         && fullstateWorld().time() == world().time()
         && world().gameMode().type() == rcsc::GameMode::PlayOn
         && world().time().cycle() % 10 == 0 )
    {
        const rcsc::WorldModel & nw = world();
        const rcsc::WorldModel & fs = fullstateWorld();
        double opp_err[3] = { 0, 0, 0 };
        int    opp_cnt[3] = { 0, 0, 0 };
        double mate_err = 0.0; int mate_cnt = 0;
        for ( int u = 1; u <= 11; ++u )
        {
            const rcsc::AbstractPlayerObject * fp = fs.theirPlayer( u );
            const rcsc::AbstractPlayerObject * np = nw.theirPlayer( u );
            if ( fp && np && np->unum() > 0 )
            {
                const double d_true = fs.self().pos().dist( fp->pos() );
                const int band = ( d_true < 10.0 ? 0 : d_true < 25.0 ? 1 : 2 );
                opp_err[band] += np->pos().dist( fp->pos() );
                opp_cnt[band] += 1;
            }
            const rcsc::AbstractPlayerObject * fm = fs.ourPlayer( u );
            const rcsc::AbstractPlayerObject * nm = nw.ourPlayer( u );
            if ( fm && nm && nm->unum() > 0 && u != nw.self().unum() )
            {
                mate_err += nm->pos().dist( fm->pos() );
                mate_cnt += 1;
            }
        }
        std::ofstream f( "noise_audit.csv", std::ios::app );
        f << nw.time().cycle() << ',' << nw.self().unum()
          << ',' << nw.ball().pos().dist( fs.ball().pos() )
          << ',' << ( nw.ball().vel() - fs.ball().vel() ).r()
          << ',' << nw.self().pos().dist( fs.self().pos() )
          << ',' << ( opp_cnt[0] ? opp_err[0] / opp_cnt[0] : -1.0 )
          << ',' << ( opp_cnt[1] ? opp_err[1] / opp_cnt[1] : -1.0 )
          << ',' << ( opp_cnt[2] ? opp_err[2] / opp_cnt[2] : -1.0 )
          << ',' << ( mate_cnt   ? mate_err   / mate_cnt   : -1.0 ) << '\n';
    }

    // AUDITORÍA NECK (temporal; track-de-ruido paso 5, 2026-07-12). Mide lo
    // que el cuello debería mantener fresco: cuando YO soy (o estoy a ≤1
    // ciclo de ser) el portador, ¿qué tan rancios están los rivales que
    // importan para decidir el pase/regate? Zona definida sobre la VERDAD
    // (fullstate) — rivales a <30m del balón real y no muy por detrás — para
    // que los fantasmas no-vistos cuenten en vez de esconderse.
    // ./neck_audit.csv: ciclo,unum,n_zona,err_medio,poscount_medio,frac_frescos(pc<=2)
    if ( config().debugFullstate()
         && fullstateWorld().time() == world().time()
         && world().gameMode().type() == rcsc::GameMode::PlayOn
         && ( world().self().isKickable()
              || world().interceptTable().selfStep() <= 1 ) )
    {
        const rcsc::WorldModel & nw = world();
        const rcsc::WorldModel & fs = fullstateWorld();
        double err_sum = 0.0; double pc_sum = 0.0;
        int n_zone = 0; int n_fresh = 0;
        for ( int u = 1; u <= 11; ++u )
        {
            const rcsc::AbstractPlayerObject * fp = fs.theirPlayer( u );
            const rcsc::AbstractPlayerObject * np = nw.theirPlayer( u );
            if ( ! fp || ! np || np->unum() <= 0 ) continue;
            if ( fp->pos().dist( fs.ball().pos() ) > 30.0 ) continue;
            if ( fp->pos().x < fs.ball().pos().x - 10.0 ) continue;
            err_sum += np->pos().dist( fp->pos() );
            pc_sum  += np->posCount();
            if ( np->posCount() <= 2 ) ++n_fresh;
            ++n_zone;
        }
        if ( n_zone > 0 )
        {
            std::ofstream f( "neck_audit.csv", std::ios::app );
            f << nw.time().cycle() << ',' << nw.self().unum()
              << ',' << n_zone
              << ',' << err_sum / n_zone
              << ',' << pc_sum / n_zone
              << ',' << static_cast<double>( n_fresh ) / n_zone << '\n';
        }
    }

    if ( this->audioSensor().trainerMessageTime() == world().time() )
    {
        std::cerr << world().ourTeamName() << ' ' << world().self().unum()
                  << ' ' << world().time()
                  << " receive trainer message["
                  << this->audioSensor().trainerMessage() << ']'
                  << std::endl;
    }


    //
    // update strategy and analyzer
    //
    Strategy::instance().update( world() );
    FieldAnalyzer::instance().update( world() );

    //
    // prepare action chain
    //
    M_field_evaluator = createFieldEvaluator();
    M_action_generator = createActionGenerator();

    ActionChainHolder::instance().setFieldEvaluator( M_field_evaluator );
    ActionChainHolder::instance().setActionGenerator( M_action_generator );

    //
    // special situations (tackle, objects accuracy, intention...)
    //
    if ( doPreprocess() )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": preprocess done" );
        return;
    }

    //
    // update action chain
    //
    ActionChainHolder::instance().update( world() );


    //
    // create current role
    //
    SoccerRole::Ptr role_ptr;
    {
        role_ptr = Strategy::i().createRole( world().self().unum(), world() );

        if ( ! role_ptr )
        {
            std::cerr << config().teamName() << ": "
                      << world().self().unum()
                      << " Error. Role is not registerd.\nExit ..."
                      << std::endl;
            M_client->setServerAlive( false );
            return;
        }
    }


    //
    // override execute if role accept
    //
    if ( role_ptr->acceptExecution( world() ) )
    {
        role_ptr->execute( this );
        return;
    }


    //
    // play_on mode
    //
    if ( world().gameMode().type() == GameMode::PlayOn )
    {
        role_ptr->execute( this );
        return;
    }


    //
    // penalty kick mode
    //
    if ( world().gameMode().isPenaltyKickMode() )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": penalty kick" );
        Bhv_PenaltyKick().execute( this );
        return;
    }

    //
    // other set play mode
    //
    Bhv_SetPlay().execute( this );
}

/*-------------------------------------------------------------------*/
/*!

 */
void
SamplePlayer::handleActionStart()
{

}

/*-------------------------------------------------------------------*/
/*!

 */
void
SamplePlayer::handleActionEnd()
{
    if ( world().self().posValid() )
    {
#if 0
        const ServerParam & SP = ServerParam::i();
        //
        // inside of pitch
        //

        // top,lower
        debugClient().addLine( Vector2D( world().ourOffenseLineX(),
                                         -SP.pitchHalfWidth() ),
                               Vector2D( world().ourOffenseLineX(),
                                         -SP.pitchHalfWidth() + 3.0 ) );
        // top,lower
        debugClient().addLine( Vector2D( world().ourDefenseLineX(),
                                         -SP.pitchHalfWidth() ),
                               Vector2D( world().ourDefenseLineX(),
                                         -SP.pitchHalfWidth() + 3.0 ) );

        // bottom,upper
        debugClient().addLine( Vector2D( world().theirOffenseLineX(),
                                         +SP.pitchHalfWidth() - 3.0 ),
                               Vector2D( world().theirOffenseLineX(),
                                         +SP.pitchHalfWidth() ) );
        //
        debugClient().addLine( Vector2D( world().offsideLineX(),
                                         world().self().pos().y - 15.0 ),
                               Vector2D( world().offsideLineX(),
                                         world().self().pos().y + 15.0 ) );

        // outside of pitch

        // top,upper
        debugClient().addLine( Vector2D( world().ourOffensePlayerLineX(),
                                         -SP.pitchHalfWidth() - 3.0 ),
                               Vector2D( world().ourOffensePlayerLineX(),
                                         -SP.pitchHalfWidth() ) );
        // top,upper
        debugClient().addLine( Vector2D( world().ourDefensePlayerLineX(),
                                         -SP.pitchHalfWidth() - 3.0 ),
                               Vector2D( world().ourDefensePlayerLineX(),
                                         -SP.pitchHalfWidth() ) );
        // bottom,lower
        debugClient().addLine( Vector2D( world().theirOffensePlayerLineX(),
                                         +SP.pitchHalfWidth() ),
                               Vector2D( world().theirOffensePlayerLineX(),
                                         +SP.pitchHalfWidth() + 3.0 ) );
        // bottom,lower
        debugClient().addLine( Vector2D( world().theirDefensePlayerLineX(),
                                         +SP.pitchHalfWidth() ),
                               Vector2D( world().theirDefensePlayerLineX(),
                                         +SP.pitchHalfWidth() + 3.0 ) );
#else
        // top,lower
        debugClient().addLine( Vector2D( world().ourDefenseLineX(),
                                         world().self().pos().y - 2.0 ),
                               Vector2D( world().ourDefenseLineX(),
                                         world().self().pos().y + 2.0 ) );

        //
        debugClient().addLine( Vector2D( world().offsideLineX(),
                                         world().self().pos().y - 15.0 ),
                               Vector2D( world().offsideLineX(),
                                         world().self().pos().y + 15.0 ) );
#endif
    }

    //
    // ball position & velocity
    //
    dlog.addText( Logger::WORLD,
                  "WM: BALL pos=(%lf, %lf), vel=(%lf, %lf, r=%lf, ang=%lf)",
                  world().ball().pos().x,
                  world().ball().pos().y,
                  world().ball().vel().x,
                  world().ball().vel().y,
                  world().ball().vel().r(),
                  world().ball().vel().th().degree() );


    dlog.addText( Logger::WORLD,
                  "WM: SELF move=(%lf, %lf, r=%lf, th=%lf)",
                  world().self().lastMove().x,
                  world().self().lastMove().y,
                  world().self().lastMove().r(),
                  world().self().lastMove().th().degree() );

    if ( world().prevBall().rpos().isValid() )
    {
        Vector2D diff = world().ball().rpos() - world().prevBall().rpos();
        dlog.addText( Logger::WORLD,
                      "WM: BALL rpos=(%lf %lf) prev_rpos=(%lf %lf) diff=(%lf %lf)",
                  world().ball().rpos().x,
                      world().ball().rpos().y,
                      world().prevBall().rpos().x,
                      world().prevBall().rpos().y,
                      diff.x,
                      diff.y );

        Vector2D ball_move = diff + world().self().lastMove();
        Vector2D diff_vel = ball_move * ServerParam::i().ballDecay();
        dlog.addText( Logger::WORLD,
                      "---> ball_move=(%lf %lf) vel=(%lf, %lf, r=%lf, th=%lf)",
                      ball_move.x,
                      ball_move.y,
                      diff_vel.x,
                      diff_vel.y,
                      diff_vel.r(),
                      diff_vel.th().degree() );
    }
}

/*-------------------------------------------------------------------*/
/*!

 */
void
SamplePlayer::handleInitMessage()
{
    {
        // Initializing the order of penalty kickers
        std::vector< int > unum_order_pk_kickers = { 10, 9, 2, 11, 3, 4, 1, 5, 6, 7, 8 };
        M_worldmodel.setPenaltyKickTakerOrder( unum_order_pk_kickers );
    }
}

/*-------------------------------------------------------------------*/
/*!

 */
void
SamplePlayer::handleServerParam()
{
    if ( ServerParam::i().keepawayMode() )
    {
        std::cerr << "set Keepaway mode communication." << std::endl;
        M_communication = Communication::Ptr( new KeepawayCommunication() );
    }
}

/*-------------------------------------------------------------------*/
/*!

 */
void
SamplePlayer::handlePlayerParam()
{
    if ( KickTable::instance().createTables() )
    {
        std::cerr << world().teamName() << ' '
                  << world().self().unum() << ": "
                  << " KickTable created."
                  << std::endl;
    }
    else
    {
        std::cerr << world().teamName() << ' '
                  << world().self().unum() << ": "
                  << " KickTable failed..."
                  << std::endl;
        M_client->setServerAlive( false );
    }
}

/*-------------------------------------------------------------------*/
/*!

 */
void
SamplePlayer::handlePlayerType()
{

}

/*-------------------------------------------------------------------*/
/*!
  communication decision.
  virtual method in super class
*/
void
SamplePlayer::communicationImpl()
{
    if ( M_communication )
    {
        M_communication->execute( this );
    }
}

/*-------------------------------------------------------------------*/
/*!
*/
bool
SamplePlayer::doPreprocess()
{
    // check tackle expires
    // check self position accuracy
    // ball search
    // check queued intention
    // check simultaneous kick

    const WorldModel & wm = this->world();

    dlog.addText( Logger::TEAM,
                  __FILE__": (doPreProcess)" );

    //
    // freezed by tackle effect
    //
    if ( wm.self().isFrozen() )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": tackle wait. expires= %d",
                      wm.self().tackleExpires() );
        // face neck to ball
        this->setViewAction( new View_Tactical() );
        this->setNeckAction( new Neck_TurnToBallOrScan( 0 ) );
        return true;
    }

    //
    // BeforeKickOff or AfterGoal. jump to the initial position
    //
    if ( wm.gameMode().type() == GameMode::BeforeKickOff
         || wm.gameMode().type() == GameMode::AfterGoal_ )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": before_kick_off" );
        Vector2D move_point =  Strategy::i().getPosition( wm.self().unum() );
        Bhv_CustomBeforeKickOff( move_point ).execute( this );
        this->setViewAction( new View_Tactical() );
        return true;
    }

    //
    // self localization error
    //
    if ( ! wm.self().posValid() )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": invalid my pos" );
        Bhv_Emergency().execute( this ); // includes change view
        return true;
    }

    //
    // ball localization error
    //
    const int count_thr = ( wm.self().goalie()
                            ? 10
                            : 5 );
    if ( wm.ball().posCount() > count_thr
         || ( wm.gameMode().type() != GameMode::PlayOn
              && wm.ball().seenPosCount() > count_thr + 10 ) )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": search ball" );
        this->setViewAction( new View_Tactical() );
        Bhv_NeckBodyToBall().execute( this );
        return true;
    }

    //
    // set default change view
    //

    this->setViewAction( new View_Tactical() );

    //
    // check shoot chance
    //
    if ( doShoot() )
    {
        return true;
    }

    //
    // check queued action
    //
    if ( this->doIntention() )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": do queued intention" );
        return true;
    }

    //
    // check simultaneous kick
    //
    if ( doForceKick() )
    {
        return true;
    }

    //
    // check pass message
    //
    if ( doHeardPassReceive() )
    {
        return true;
    }

    return false;
}

/*-------------------------------------------------------------------*/
/*!

*/
bool
SamplePlayer::doShoot()
{
    const WorldModel & wm = this->world();

    if ( wm.gameMode().type() != GameMode::IndFreeKick_
         && wm.time().stopped() == 0
         && wm.self().isKickable()
         && Bhv_StrictCheckShoot().execute( this ) )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": shooted" );

        // reset intention
        this->setIntention( static_cast< SoccerIntention * >( 0 ) );
        return true;
    }

    return false;
}

/*-------------------------------------------------------------------*/
/*!

*/
bool
SamplePlayer::doForceKick()
{
    const WorldModel & wm = this->world();

    if ( wm.gameMode().type() == GameMode::PlayOn
         && ! wm.self().goalie()
         && wm.self().isKickable()
         && wm.kickableOpponent() )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": simultaneous kick" );
        this->debugClient().addMessage( "SimultaneousKick" );
        Vector2D goal_pos( ServerParam::i().pitchHalfLength(), 0.0 );

        if ( wm.self().pos().x > 36.0
             && wm.self().pos().absY() > 10.0 )
        {
            goal_pos.x = 45.0;
            dlog.addText( Logger::TEAM,
                          __FILE__": simultaneous kick cross type" );
        }
        Body_KickOneStep( goal_pos,
                          ServerParam::i().ballSpeedMax()
                          ).execute( this );
        this->setNeckAction( new Neck_ScanField() );
        return true;
    }

    return false;
}

/*-------------------------------------------------------------------*/
/*!

*/
bool
SamplePlayer::doHeardPassReceive()
{
    const WorldModel & wm = this->world();

    if ( wm.audioMemory().passTime() != wm.time()
         || wm.audioMemory().pass().empty()
         || wm.audioMemory().pass().front().receiver_ != wm.self().unum() )
    {

        return false;
    }

    int self_min = wm.interceptTable().selfStep();
    Vector2D intercept_pos = wm.ball().inertiaPoint( self_min );
    Vector2D heard_pos = wm.audioMemory().pass().front().receive_pos_;

    dlog.addText( Logger::TEAM,
                  __FILE__":  (doHeardPassReceive) heard_pos(%.2f %.2f) intercept_pos(%.2f %.2f)",
                  heard_pos.x, heard_pos.y,
                  intercept_pos.x, intercept_pos.y );

    if ( ! wm.kickableTeammate()
         && wm.ball().posCount() <= 1
         && wm.ball().velCount() <= 1
         && self_min < 20
         //&& intercept_pos.dist( heard_pos ) < 3.0 ) //5.0 )
         )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": (doHeardPassReceive) intercept cycle=%d. intercept",
                      self_min );
        this->debugClient().addMessage( "Comm:Receive:Intercept" );
        Body_Intercept().execute( this );
        this->setNeckAction( new Neck_TurnToBall() );
    }
    else
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": (doHeardPassReceive) intercept cycle=%d. go to receive point",
                      self_min );
        this->debugClient().setTarget( heard_pos );
        this->debugClient().addMessage( "Comm:Receive:GoTo" );
        Body_GoToPoint( heard_pos,
                    0.5,
                        ServerParam::i().maxDashPower()
                        ).execute( this );
        this->setNeckAction( new Neck_TurnToBall() );
    }

    this->setIntention( new IntentionReceive( heard_pos,
                                              ServerParam::i().maxDashPower(),
                                              0.9,
                                              5,
                                              wm.time() ) );

    return true;
}

/*-------------------------------------------------------------------*/
/*!

*/
FieldEvaluator::ConstPtr
SamplePlayer::getFieldEvaluator() const
{
    return M_field_evaluator;
}

/*-------------------------------------------------------------------*/
/*!

*/
FieldEvaluator::ConstPtr
SamplePlayer::createFieldEvaluator() const
{
    return FieldEvaluator::ConstPtr( new SampleFieldEvaluator );
}


/*-------------------------------------------------------------------*/
/*!
*/
#include "actgen_cross.h"
#include "actgen_direct_pass.h"
#include "actgen_self_pass.h"
#include "actgen_strict_check_pass.h"
#include "actgen_short_dribble.h"
#include "actgen_simple_dribble.h"
#include "actgen_shoot.h"
#include "actgen_action_chain_length_filter.h"

ActionGenerator::ConstPtr
SamplePlayer::createActionGenerator() const
{
    CompositeActionGenerator * g = new CompositeActionGenerator();

    //
    // shoot
    //
    g->addGenerator( new ActGen_RangeActionChainLengthFilter
                     ( new ActGen_Shoot(),
                       2, ActGen_RangeActionChainLengthFilter::MAX ) );

    //
    // strict check pass
    //
    g->addGenerator( new ActGen_MaxActionChainLengthFilter
                     ( new ActGen_StrictCheckPass(), 1 ) );

    //
    // cross
    //
    // O3 (2026-07-05): 1→2 — habilita el plan "pase al extremo → centro atrás"
    // (el cutback real es a 2 toques y antes ni se enumeraba).
    g->addGenerator( new ActGen_MaxActionChainLengthFilter
                     ( new ActGen_Cross(), 2 ) );

    //
    // direct pass
    //
    g->addGenerator( new ActGen_RangeActionChainLengthFilter
                     ( new ActGen_DirectPass(),
                       2, ActGen_RangeActionChainLengthFilter::MAX ) );

    //
    // short dribble
    //
    g->addGenerator( new ActGen_MaxActionChainLengthFilter
                     ( new ActGen_ShortDribble(), 1 ) );

    //
    // self pass (long dribble)
    //
    g->addGenerator( new ActGen_MaxActionChainLengthFilter
                     ( new ActGen_SelfPass(), 1 ) );

    //
    // simple dribble
    //
    g->addGenerator( new ActGen_RangeActionChainLengthFilter
                     ( new ActGen_SimpleDribble(),
                       2, ActGen_RangeActionChainLengthFilter::MAX ) );

    return ActionGenerator::ConstPtr( g );
}

/*-------------------------------------------------------------------*/
// Denoiser de localización (port Cyrus 2026-07-11).
void
SamplePlayer::update_player_by_denoiser()
{
    M_localization_denoiser->update( this );
    M_localization_denoiser->debug( this );
}
