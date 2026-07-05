// -*-c++-*-

/*!
  \file strategy.cpp
  \brief team strategh Source File
*/

/*
 *Copyright:

 Cyrus2D
 Modified by Omid Amini, Nader Zare
 
 Gliders2d
 Modified by Mikhail Prokopenko, Peter Wang

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

#include "strategy.h"

#include "soccer_role.h"


#ifndef USE_GENERIC_FACTORY
#include "role_sample.h"

#include "role_center_back.h"
#include "role_center_forward.h"
#include "role_defensive_half.h"
#include "role_goalie.h"
#include "role_offensive_half.h"
#include "role_side_back.h"
#include "role_side_forward.h"
#include "role_side_half.h"

#include "role_keepaway_keeper.h"
#include "role_keepaway_taker.h"
#endif

#include <rcsc/player/intercept_table.h>
#include <rcsc/player/world_model.h>
#include <rcsc/geom/voronoi_diagram.h>

#include <rcsc/formation/formation_parser.h>
#include <rcsc/common/logger.h>
#include <rcsc/common/server_param.h>
#include <rcsc/param/cmd_line_parser.h>
#include <rcsc/param/param_map.h>
#include <rcsc/game_mode.h>

#include <iostream>

using namespace rcsc;

const std::string Strategy::BEFORE_KICK_OFF_CONF = "before-kick-off.conf";
const std::string Strategy::BEFORE_KICK_OFF_OUR_CONF = "before-kick-off-our.conf";
const std::string Strategy::NORMAL_FORMATION_CONF = "normal-formation.conf";
const std::string Strategy::DEFENSE_FORMATION_CONF = "defense-formation.conf";
const std::string Strategy::OFFENSE_FORMATION_CONF = "offense-formation.conf";
const std::string Strategy::GOAL_KICK_OPP_FORMATION_CONF = "goal-kick-opp.conf";
const std::string Strategy::GOAL_KICK_OUR_FORMATION_CONF = "goal-kick-our.conf";
const std::string Strategy::GOALIE_CATCH_OPP_FORMATION_CONF = "goalie-catch-opp.conf";
const std::string Strategy::GOALIE_CATCH_OUR_FORMATION_CONF = "goalie-catch-our.conf";
const std::string Strategy::KICKIN_OUR_FORMATION_CONF = "kickin-our-formation.conf";
const std::string Strategy::SETPLAY_OPP_FORMATION_CONF = "setplay-opp-formation.conf";
const std::string Strategy::SETPLAY_OUR_FORMATION_CONF = "setplay-our-formation.conf";
const std::string Strategy::INDIRECT_FREEKICK_OPP_FORMATION_CONF = "indirect-freekick-opp-formation.conf";
const std::string Strategy::INDIRECT_FREEKICK_OUR_FORMATION_CONF = "indirect-freekick-our-formation.conf";
const std::string Strategy::AFTER_GOAL_FORMATION_CONF_Left = "after-goal-formation-r-left.conf";
const std::string Strategy::AFTER_GOAL_FORMATION_CONF_Right = "after-goal-formation-r-right.conf";
const std::string Strategy::AFTER_GOAL_FORMATION_CONF_T_Left = "after-goal-formation-t-left.conf";
const std::string Strategy::AFTER_GOAL_FORMATION_CONF_T_Right = "after-goal-formation-t-right.conf";

/*-------------------------------------------------------------------*/
/*!

 */
namespace {
struct MyCompare {

    const Vector2D pos_;

    MyCompare( const Vector2D & pos )
        : pos_( pos )
      { }

    bool operator()( const Vector2D & lhs,
                     const Vector2D & rhs ) const
      {
          return (lhs - pos_).length() < (rhs - pos_).length();
      }
};
}

/*-------------------------------------------------------------------*/
/*!

 */
Strategy::Strategy()
    : M_goalie_unum( Unum_Unknown ),
      M_current_situation( Normal_Situation ),
      M_role_number( 11, 0 ),
      M_position_types( 11, Position_Center ),
      M_positions( 11 ),
      M_taunt_phase( -1 ),
      M_taunt_counter( 0 )
{
#ifndef USE_GENERIC_FACTORY
    //
    // roles
    //

    M_role_factory[RoleSample::name()] = &RoleSample::create;

    M_role_factory[RoleGoalie::name()] = &RoleGoalie::create;
    M_role_factory[RoleCenterBack::name()] = &RoleCenterBack::create;
    M_role_factory[RoleSideBack::name()] = &RoleSideBack::create;
    M_role_factory[RoleDefensiveHalf::name()] = &RoleDefensiveHalf::create;
    M_role_factory[RoleOffensiveHalf::name()] = &RoleOffensiveHalf::create;
    M_role_factory[RoleSideHalf::name()] = &RoleSideHalf::create;
    M_role_factory[RoleSideForward::name()] = &RoleSideForward::create;
    M_role_factory[RoleCenterForward::name()] = &RoleCenterForward::create;

    // keepaway
    M_role_factory[RoleKeepawayKeeper::name()] = &RoleKeepawayKeeper::create;
    M_role_factory[RoleKeepawayTaker::name()] = &RoleKeepawayTaker::create;

#endif

    for ( size_t i = 0; i < M_role_number.size(); ++i )
    {
        M_role_number[i] = i + 1;
    }
}

/*-------------------------------------------------------------------*/
/*!

 */
Strategy &
Strategy::instance()
{
    static Strategy s_instance;
    return s_instance;
}

/*-------------------------------------------------------------------*/
/*!

 */
bool
Strategy::init( CmdLineParser & cmd_parser )
{
    ParamMap param_map( "HELIOS_base options" );

    // std::string fconf;
    //param_map.add()
    //    ( "fconf", "", &fconf, "another formation file." );

    //
    //
    //

    if ( cmd_parser.count( "help" ) > 0 )
    {
        param_map.printHelp( std::cout );
        return false;
    }

    //
    //
    //

    cmd_parser.parse( param_map );

    return true;
}

/*-------------------------------------------------------------------*/
/*!

 */
bool
Strategy::read( const std::string & formation_dir )
{
    static bool s_initialized = false;

    if ( s_initialized )
    {
        std::cerr << __FILE__ << ' ' << __LINE__ << ": already initialized."
                  << std::endl;
        return false;
    }

    std::string configpath = formation_dir;
    if ( ! configpath.empty()
         && configpath[ configpath.length() - 1 ] != '/' )
    {
        configpath += '/';
    }

    // before kick off
    M_before_kick_off_formation = createFormation( configpath + BEFORE_KICK_OFF_CONF );
    if ( ! M_before_kick_off_formation )
    {
        std::cerr << "Failed to read before_kick_off formation" << std::endl;
        return false;
    }
    // saque propio: variante con el pateador junto al balón (opcional,
    // si falta el archivo se usa la formación de kick-off normal)
    M_before_kick_off_our_formation = createFormation( configpath + BEFORE_KICK_OFF_OUR_CONF );
    if ( ! M_before_kick_off_our_formation )
    {
        std::cerr << "before-kick-off-our.conf not found, using before-kick-off.conf"
                  << std::endl;
        M_before_kick_off_our_formation = M_before_kick_off_formation;
    }

    // Cargar formaciones para secuencia de taunts R → T
    M_after_goal_r_left_formation = createFormation( configpath + AFTER_GOAL_FORMATION_CONF_Left );
    if ( ! M_after_goal_r_left_formation )
    {
        std::cerr << "Failed to read after-goal R LEFT formation from " << configpath + AFTER_GOAL_FORMATION_CONF_Left << std::endl;
    }

    M_after_goal_r_right_formation = createFormation( configpath + AFTER_GOAL_FORMATION_CONF_Right );
    if ( ! M_after_goal_r_right_formation )
    {
        std::cerr << "Failed to read after-goal R RIGHT formation from " << configpath + AFTER_GOAL_FORMATION_CONF_Right << std::endl;
    }

    M_after_goal_t_left_formation = createFormation( configpath + AFTER_GOAL_FORMATION_CONF_T_Left );
    if ( ! M_after_goal_t_left_formation )
    {
        std::cerr << "Failed to read after-goal T LEFT formation from " << configpath + AFTER_GOAL_FORMATION_CONF_T_Left << std::endl;
    }

    M_after_goal_t_right_formation = createFormation( configpath + AFTER_GOAL_FORMATION_CONF_T_Right );
    if ( ! M_after_goal_t_right_formation )
    {
        std::cerr << "Failed to read after-goal T RIGHT formation from " << configpath + AFTER_GOAL_FORMATION_CONF_T_Right << std::endl;
    }

    ///////////////////////////////////////////////////////////
    M_normal_formation = createFormation( configpath + NORMAL_FORMATION_CONF );
    if ( ! M_normal_formation )
    {
        std::cerr << "Failed to read normal formation" << std::endl;
        return false;
    }

    M_defense_formation = createFormation( configpath + DEFENSE_FORMATION_CONF );
    if ( ! M_defense_formation )
    {
        std::cerr << "Failed to read defense formation" << std::endl;
        return false;
    }

    M_offense_formation = createFormation( configpath + OFFENSE_FORMATION_CONF );
    if ( ! M_offense_formation )
    {
        std::cerr << "Failed to read offense formation" << std::endl;
        return false;
    }

    M_goal_kick_opp_formation = createFormation( configpath + GOAL_KICK_OPP_FORMATION_CONF );
    if ( ! M_goal_kick_opp_formation )
    {
        return false;
    }

    M_goal_kick_our_formation = createFormation( configpath + GOAL_KICK_OUR_FORMATION_CONF );
    if ( ! M_goal_kick_our_formation )
    {
        return false;
    }

    M_goalie_catch_opp_formation = createFormation( configpath + GOALIE_CATCH_OPP_FORMATION_CONF );
    if ( ! M_goalie_catch_opp_formation )
    {
        return false;
    }

    M_goalie_catch_our_formation = createFormation( configpath + GOALIE_CATCH_OUR_FORMATION_CONF );
    if ( ! M_goalie_catch_our_formation )
    {
        return false;
    }

    M_kickin_our_formation = createFormation( configpath + KICKIN_OUR_FORMATION_CONF );
    if ( ! M_kickin_our_formation )
    {
        std::cerr << "Failed to read kickin our formation" << std::endl;
        return false;
    }

    M_setplay_opp_formation = createFormation( configpath + SETPLAY_OPP_FORMATION_CONF );
    if ( ! M_setplay_opp_formation )
    {
        std::cerr << "Failed to read setplay opp formation" << std::endl;
        return false;
    }

    M_setplay_our_formation = createFormation( configpath + SETPLAY_OUR_FORMATION_CONF );
    if ( ! M_setplay_our_formation )
    {
        std::cerr << "Failed to read setplay our formation" << std::endl;
        return false;
    }

    M_indirect_freekick_opp_formation = createFormation( configpath + INDIRECT_FREEKICK_OPP_FORMATION_CONF );
    if ( ! M_indirect_freekick_opp_formation )
    {
        std::cerr << "Failed to read indirect freekick opp formation" << std::endl;
        return false;
    }

    M_indirect_freekick_our_formation = createFormation( configpath + INDIRECT_FREEKICK_OUR_FORMATION_CONF );
    if ( ! M_indirect_freekick_our_formation )
    {
        std::cerr << "Failed to read indirect freekick our formation" << std::endl;
        return false;
    }


    s_initialized = true;
    return true;
}

/*-------------------------------------------------------------------*/
/*!

 */
Formation::Ptr
Strategy::createFormation( const std::string & filepath )
{
    Formation::Ptr f = FormationParser::parse( filepath );

    if ( ! f )
    {
        std::cerr << "(Strategy::createFormation) Could not create a formation from " << filepath << std::endl;
        return Formation::Ptr();
    }

    //
    // check role names
    //
    for ( int unum = 1; unum <= 11; ++unum )
    {
        const std::string role_name = f->roleName( unum );
        if ( role_name == "Savior"
             || role_name == "Goalie" )
        {
            if ( M_goalie_unum == Unum_Unknown )
            {
                M_goalie_unum = unum;
            }

            if ( M_goalie_unum != unum )
            {
                std::cerr << __FILE__ << ':' << __LINE__ << ':'
                          << " ***ERROR*** Illegal goalie's uniform number"
                          << " read unum=" << unum
                          << " expected=" << M_goalie_unum
                          << std::endl;
                f.reset();
                return f;
            }
        }


#ifdef USE_GENERIC_FACTORY
        SoccerRole::Ptr role = SoccerRole::create( role_name );
        if ( ! role )
        {
            std::cerr << __FILE__ << ':' << __LINE__ << ':'
                      << " ***ERROR*** Unsupported role name ["
                      << role_name << "] is appered in ["
                      << filepath << "]" << std::endl;
            f.reset();
            return f;
        }
#else
        if ( M_role_factory.find( role_name ) == M_role_factory.end() )
        {
            std::cerr << __FILE__ << ':' << __LINE__ << ':'
                      << " ***ERROR*** Unsupported role name ["
                      << role_name << "] is appered in ["
                      << filepath << "]" << std::endl;
            f.reset();
            return f;
        }
#endif
    }

    return f;
}

/*-------------------------------------------------------------------*/
/*!

 */
void
Strategy::update( const WorldModel & wm )
{
    static GameTime s_update_time( -1, 0 );

    if ( s_update_time == wm.time() )
    {
        return;
    }
    s_update_time = wm.time();

    updateSituation( wm );
    updatePosition( wm );
}

/*-------------------------------------------------------------------*/
/*!

 */
void
Strategy::exchangeRole( const int unum0,
                        const int unum1 )
{
    if ( unum0 < 1 || 11 < unum0
         || unum1 < 1 || 11 < unum1 )
    {
        std::cerr << __FILE__ << ':' << __LINE__ << ':'
                  << "(exchangeRole) Illegal uniform number. "
                  << unum0 << ' ' << unum1
                  << std::endl;
        dlog.addText( Logger::TEAM,
                      __FILE__":(exchangeRole) Illegal unum. %d %d",
                      unum0, unum1 );
        return;
    }

    if ( unum0 == unum1 )
    {
        std::cerr << __FILE__ << ':' << __LINE__ << ':'
                  << "(exchangeRole) same uniform number. "
                  << unum0 << ' ' << unum1
                  << std::endl;
        dlog.addText( Logger::TEAM,
                      __FILE__":(exchangeRole) same unum. %d %d",
                      unum0, unum1 );
        return;
    }

    int role0 = M_role_number[unum0 - 1];
    int role1 = M_role_number[unum1 - 1];

    dlog.addText( Logger::TEAM,
                  __FILE__":(exchangeRole) unum=%d(role=%d) <-> unum=%d(role=%d)",
                  unum0, role0,
                  unum1, role1 );

    M_role_number[unum0 - 1] = role1;
    M_role_number[unum1 - 1] = role0;
}

/*-------------------------------------------------------------------*/
/*!

*/
bool
Strategy::isMarkerType( const int unum ) const
{
    int number = roleNumber( unum );

    if ( number == 2
         || number == 3
         || number == 4
         || number == 5 )
    {
        return true;
    }

    return false;
}

/*-------------------------------------------------------------------*/
/*!

 */
SoccerRole::Ptr
Strategy::createRole( const int unum,
                      const WorldModel & world ) const
{
    const int number = roleNumber( unum );

    SoccerRole::Ptr role;

    if ( number < 1 || 11 < number )
    {
        std::cerr << __FILE__ << ": " << __LINE__
                  << " ***ERROR*** Invalid player number " << number
                  << std::endl;
        return role;
    }

    Formation::Ptr f = getFormation( world );
    if ( ! f )
    {
        std::cerr << __FILE__ << ": " << __LINE__
                  << " ***ERROR*** faled to create role. Null formation" << std::endl;
        return role;
    }

    const std::string role_name = f->roleName( number );

#ifdef USE_GENERIC_FACTORY
    role = SoccerRole::create( role_name );
#else
    RoleFactory::const_iterator factory = M_role_factory.find( role_name );
    if ( factory != M_role_factory.end() )
    {
        role = factory->second();
    }
#endif

    if ( ! role )
    {
        std::cerr << __FILE__ << ": " << __LINE__
                  << " ***ERROR*** unsupported role name ["
                  << role_name << "]"
                  << std::endl;
    }
    return role;
}

/*-------------------------------------------------------------------*/
/*!

 */
void
Strategy::updateSituation( const WorldModel & wm )
{
    // Do NOT reset to Normal here — preserve previous value for hysteresis.
    // The previous M_current_situation is the default unless explicitly changed.

    if ( wm.gameMode().type() != GameMode::PlayOn )
    {
        if ( wm.gameMode().isPenaltyKickMode() )
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": Situation PenaltyKick" );
            M_current_situation = PenaltyKick_Situation;
        }
        else if ( wm.gameMode().isOurSetPlay( wm.ourSide() ) )
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": Situation OurSetPlay" );
            M_current_situation = OurSetPlay_Situation;
        }
        else
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": Situation OppSetPlay" );
            M_current_situation = OppSetPlay_Situation;
        }
        return;
    }

    // ── Score-based situation override ───────────────────────────────────
    const int our_score = ( wm.ourSide() == LEFT
                            ? wm.gameMode().scoreLeft()
                            : wm.gameMode().scoreRight() );
    const int opp_score = ( wm.ourSide() == LEFT
                            ? wm.gameMode().scoreRight()
                            : wm.gameMode().scoreLeft() );
    const int score_diff    = our_score - opp_score;
    // actualHalfTime() está en ciclos (3000); halfTime() son segundos (300)
    // y hacía que "últimos 400 ciclos" se activara desde el ciclo 200.
    const int total_cycles  = ServerParam::i().actualHalfTime()
                              * ServerParam::i().nrNormalHalfs();
    const int remaining     = total_cycles - wm.time().cycle();

    int self_min = wm.interceptTable().selfStep();
    int mate_min = wm.interceptTable().teammateStep();
    int opp_min = wm.interceptTable().opponentStep();
    int our_min = std::min( self_min, mate_min );
    const double ball_x = wm.ball().pos().x;

    // Overrides por marcador: SOLO si disputamos el balón. Sin esta condición,
    // ir perdiendo 2+ forzaba Offense_Situation todo el partido aunque el
    // rival tuviera el balón → formación ofensiva defendiendo, estructura rota
    // y espiral de fatiga (se vio vs RoboCIn perdiendo 0-2).
    const bool ball_contested = ( our_min <= opp_min + 2 );

    // Losing by 2+ → push to attack while we contest the ball
    if ( score_diff <= -2 && ball_contested )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": Situation Offense (losing %d, urgent attack)",
                      -score_diff );
        M_current_situation = Offense_Situation;
        return;
    }

    // Losing by 1 in the last 400 cycles → attack to equalize
    if ( score_diff == -1 && remaining <= 400 && ball_contested )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": Situation Offense (losing 1, %d cycles left)",
                      remaining );
        M_current_situation = Offense_Situation;
        return;
    }

    // ── Defense trigger ───────────────────────────────────────────────
    // Enter defense when:
    //   (a) opponent winning ball race (no cycle advantage needed), OR
    //   (b) ball is in our half and situation is contested (within 2 cycles)
    // Hysteresis: once in defense, exit ONLY when we have clear ball advantage (3+ cycles).
    const bool opp_winning  = ( opp_min <= our_min );
    const bool ball_our_half_contested = ( ball_x < 0.0 && opp_min <= our_min + 2 );

    if ( opp_winning || ball_our_half_contested )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": Situation Defense (opp_min=%d our_min=%d ball_x=%.1f)",
                      opp_min, our_min, ball_x );
        M_current_situation = Defense_Situation;
        return;
    }

    // Hysteresis: if already defending, stay until we clearly win ball (3-cycle gap)
    // BUG corregido: la condición estaba invertida (our_min < opp_min + 3 es
    // cierta sobre todo CON posesión nuestra) → el equipo quedaba atrapado en
    // formación defensiva mientras atacaba. Permanecer en Defense solo
    // mientras NO tengamos ventaja clara de 3+ ciclos al balón.
    if ( M_current_situation == Defense_Situation && our_min > opp_min - 3 )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": Situation Defense (hysteresis, our_min=%d opp_min=%d)",
                      our_min, opp_min );
        return;
    }

    // ── Offense trigger ───────────────────────────────────────────────
    const int offense_threshold = ( score_diff >= 2 ) ? 3 : 2;
    if ( our_min <= opp_min - offense_threshold )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": Situation Offense (thr=%d score=%d)", offense_threshold, score_diff );
        M_current_situation = Offense_Situation;
        return;
    }

    dlog.addText( Logger::TEAM,
                  __FILE__": Situation Normal" );
    M_current_situation = Normal_Situation;
}

/*-------------------------------------------------------------------*/
/*!

 */
void
Strategy::updatePosition( const WorldModel & wm )
{
    static GameTime s_update_time( 0, 0 );
    if ( s_update_time == wm.time() )
    {
        return;
    }
    s_update_time = wm.time();

    Formation::Ptr f = getFormation( wm );
    if ( ! f )
    {
        std::cerr << wm.teamName() << ':' << wm.self().unum() << ": "
                  << wm.time()
                  << " ***ERROR*** could not get the current formation" << std::endl;
        return;
    }

    int ball_step = 0;
    if ( wm.gameMode().type() == GameMode::PlayOn
         || wm.gameMode().type() == GameMode::GoalKick_ )
    {
        ball_step = std::min( 1000, wm.interceptTable().teammateStep() );
        ball_step = std::min( ball_step, wm.interceptTable().opponentStep() );
        ball_step = std::min( ball_step, wm.interceptTable().selfStep() );
    }

    Vector2D ball_pos = wm.ball().inertiaPoint( ball_step );

    dlog.addText( Logger::TEAM,
                  __FILE__": HOME POSITION: ball pos=(%.1f %.1f) step=%d",
                  ball_pos.x, ball_pos.y,
                  ball_step );

    M_positions.clear();
    f->getPositions( ball_pos, M_positions );
    applyDynamicFormationShifts( wm, ball_pos );

    // G2d: various states
    bool indFK = false;
    if ( ( wm.gameMode().type() == GameMode::BackPass_
           && wm.gameMode().side() == wm.theirSide() )
         || ( wm.gameMode().type() == GameMode::IndFreeKick_
              && wm.gameMode().side() == wm.ourSide() ) 
         || ( wm.gameMode().type() == GameMode::FoulCharge_
              && wm.gameMode().side() == wm.theirSide() )
         || ( wm.gameMode().type() == GameMode::FoulPush_
              && wm.gameMode().side() == wm.theirSide() )
        )
        indFK = true;

    bool dirFK = false;
    if ( 
          ( wm.gameMode().type() == GameMode::FreeKick_
              && wm.gameMode().side() == wm.ourSide() ) 
         || ( wm.gameMode().type() == GameMode::FoulCharge_
              && wm.gameMode().side() == wm.theirSide() )
         || ( wm.gameMode().type() == GameMode::FoulPush_
              && wm.gameMode().side() == wm.theirSide() )
        )
        dirFK = true;

    bool cornerK = false;
    if ( 
          ( wm.gameMode().type() == GameMode::CornerKick_
              && wm.gameMode().side() == wm.ourSide() ) 
        )
        cornerK = true;

    bool kickin = false;
    if ( 
          ( wm.gameMode().type() == GameMode::KickIn_
              && wm.gameMode().side() == wm.ourSide() ) 
        )
        kickin = true;


    // C2D: Helios 18 Tune removed -> replace with BNN
	// bool heliosbase = false;
	// bool helios2018 = false;
	// if (wm.opponentTeamName().find("HELIOS_base") != std::string::npos)
	// 	heliosbase = true;
	// else if (wm.opponentTeamName().find("HELIOS2018") != std::string::npos)
	// 	helios2018 = true;

    if ( ServerParam::i().useOffside() )
    {
        double max_x = wm.offsideLineX();
        if ( ServerParam::i().kickoffOffside()
             && ( wm.gameMode().type() == GameMode::BeforeKickOff
                  || wm.gameMode().type() == GameMode::AfterGoal_ ) )
        {
            max_x = 0.0;
        }
        else
        {
            int mate_step = wm.interceptTable().teammateStep();
            if ( mate_step < 50 )
            {
                Vector2D trap_pos = wm.ball().inertiaPoint( mate_step );
                if ( trap_pos.x > max_x ) max_x = trap_pos.x;
            }

            max_x -= 1.0;
        }
    // C2d: PlayerPtrCont::const_iterator replace with auto
    // G2d: Voronoi diagram
			bool newvel = false;

                        VoronoiDiagram vd;
                        // const ServerParam & SP = ServerParam::i();

                        std::vector<Vector2D> vd_cont;
                        std::vector<Vector2D> NOL_cont;  // Near Offside Line
                        std::vector<Vector2D> NOL_tmp;  // Near Offside Line tmp

                        std::vector<Vector2D> OffsideSegm_cont;
                        std::vector<Vector2D> OffsideSegm_tmpcont;

                        Vector2D y1( wm.offsideLineX(), -34.0);
                        Vector2D y2( wm.offsideLineX(), 34.0);

                        if (wm.ball().pos().x > 25.0)
                        {
                                if (wm.ball().pos().y < 0.0)
                                        y2.y = 20.0;
                                if (wm.ball().pos().y > 0.0)
                                        y1.y = -20.0;
                        }

                        if (wm.ball().pos().x > 36.0)
                        {
                                if (wm.ball().pos().y < 0.0)
                                        y2.y = 8.0;
                                if (wm.ball().pos().y > 0.0)
                                        y1.y = -8.0;
                        }

                        if (wm.ball().pos().x > 49.0)
                        {
                                y1.x = y1.x - 4.0;
                                y2.x = y2.x - 4.0;
                        }

                        for ( auto o = wm.opponentsFromSelf().begin();
                                o != wm.opponentsFromSelf().end();
                                ++o )
                        {
                                if (newvel)
                                           vd.addPoint((*o)->pos() + (*o)->vel());
                                else
                                           vd.addPoint((*o)->pos());
                        }

                        if (y1.x < 37.0)
                        {
                                   vd.addPoint(y1);
                                   vd.addPoint(y2);
                        }

                                vd.compute();


                        Line2D offsideLine (y1, y2);

                            for ( VoronoiDiagram::Segment2DCont::const_iterator p = vd.segments().begin(),
                                      end = vd.segments().end();
                                          p != end;
                                          ++p )
                            {
                                Vector2D si = (*p).intersection( offsideLine );
                                if (si.isValid() && fabs(si.y) < 34.0 && fabs(si.x) < 52.5)
                                {
                                        OffsideSegm_tmpcont.push_back(si);

                                }
                            }

                            std::sort( OffsideSegm_tmpcont.begin(), OffsideSegm_tmpcont.end(), MyCompare( wm.ball().pos() ) );

                            double prevY = -1000.0;

                                for ( std::vector<Vector2D>::iterator p = OffsideSegm_tmpcont.begin(),
                                      end = OffsideSegm_tmpcont.end();
                                          p != end;
                                          ++p )
                                {
                                    if ( p == OffsideSegm_tmpcont.begin() )
                                    {
                                        OffsideSegm_cont.push_back((*p));
                                        prevY = (*p).y;
                                        continue;
                                    }

                                    if ( fabs ( (*p).y - prevY ) > 2.0  )
                                    {
                                        prevY = (*p).y;
                                        OffsideSegm_cont.push_back((*p));
                                    }
                                }


                            // int n_points = 0;

                            for ( VoronoiDiagram::Vector2DCont::const_iterator p = vd.vertices().begin(),
                                      end = vd.vertices().end();
                                          p != end;
                                          ++p )
                            {
                                if ( (*p).x < wm.offsideLineX() - 5.0  && (*p).x > 0.0 )
                                {
                                        vd_cont.push_back((*p));

                                }
                            }

        // end of Voronoi

        // G2d: assign players to Voronoi points

                            Vector2D rank (y1.x, -34.0);

                            Vector2D first_pt (-100.0, -100.0);
                            Vector2D mid_pt (-100.0, -100.0);
                            Vector2D third_pt (-100.0, -100.0);

                            if (wm.ball().pos().y > 0.0)
                                rank.y = 34.0;

                            std::sort( OffsideSegm_cont.begin(), OffsideSegm_cont.end(), MyCompare( rank ) );

                            // int shift = 0;

                            // if (OffsideSegm_cont.size() > 4)
                                // shift = 1;

                            if (OffsideSegm_cont.size() > 0)
                                first_pt = OffsideSegm_cont[0];

                            if (OffsideSegm_cont.size() > 1)
                                third_pt = OffsideSegm_cont[OffsideSegm_cont.size() - 1];

                            if (OffsideSegm_cont.size() > 2)
                                mid_pt = OffsideSegm_cont[2];

                            // P1 (2026-07-03): reservar SEGUNDO PALO/centro en
                            // la asignación profunda. Los huecos salen ordenados
                            // desde la esquina del lado del balón: con el ataque
                            // cargado a la banda los 3 puntos quedan laterales y
                            // nadie ocupa el centro (medido: 29-40% de los ciclos
                            // con balón en x>36 SIN jugadores en zona de remate).
                            // Si ningún punto es central (|y|<10) ni del lado
                            // contrario, el tercero (el delantero del lado débil)
                            // se recoloca al segundo palo sobre la misma línea.
                            {
                                const double ball_side =
                                    ( wm.ball().pos().y >= 0.0 ? 1.0 : -1.0 );
                                auto central_or_far = [&]( const Vector2D & p ) {
                                    return p.x > -1.0
                                        && ( fabs( p.y ) < 10.0
                                             || p.y * ball_side < -3.0 );
                                };
                                if ( ! central_or_far( first_pt )
                                     && ! central_or_far( mid_pt )
                                     && ! central_or_far( third_pt ) )
                                {
                                    const double post_x =
                                        ( third_pt.x > -1.0 ? third_pt.x
                                          : ( first_pt.x > -1.0 ? first_pt.x
                                              : y1.x ) );
                                    third_pt = Vector2D( post_x, -ball_side * 7.0 );
                                }
                            }

                            int first_unum = -1;
                            int sec_unum = -1;
                            int third_unum = -1;

                            if (wm.ball().pos().y <= 0.0)
                            {
                                double tmp = 100.0;
                                for ( int ch = 9; ch <= 11; ch++ )
                                {
                                        if ( wm.ourPlayer(ch) == NULL ) 
                                                continue;

                                        if (wm.ourPlayer(ch)->pos().y < tmp)
                                        {
                                                tmp = wm.ourPlayer(ch)->pos().y;
                                                first_unum = ch;
                                        }
                                }

                                tmp = 100.0;

                                for ( int ch = 9; ch <= 11; ch++ )
                                {
                                        if ( wm.ourPlayer(ch) == NULL ) 
                                                continue;

                                        if (ch == first_unum)
                                                continue;

                                        if (wm.ourPlayer(ch)->pos().y < tmp)
                                        {
                                                tmp = wm.ourPlayer(ch)->pos().y;
                                                sec_unum = ch;
                                        }
                                }

                                for ( int ch = 9; ch <= 11; ch++ )
                                {
                                        if (ch == first_unum || ch == sec_unum)
                                                continue;

                                        if (first_unum > 0 && sec_unum > 0)
                                                third_unum = ch;
                                }
                            }

                            if (wm.ball().pos().y > 0.0)
                            {
                                double tmp = -100.0;
                                for ( int ch = 9; ch <= 11; ch++ )
                                {
                                        if ( wm.ourPlayer(ch) == NULL ) 
                                                continue;

                                        if (wm.ourPlayer(ch)->pos().y > tmp)
                                        {
                                                tmp = wm.ourPlayer(ch)->pos().y;
                                                first_unum = ch;
                                        }
                                }

                                tmp = -100.0;

                                for ( int ch = 9; ch <= 11; ch++ )
                                {
                                        if ( wm.ourPlayer(ch) == NULL ) 
                                                continue;

                                        if (ch == first_unum)
                                                continue;

                                        if (wm.ourPlayer(ch)->pos().y > tmp)
                                        {
                                                tmp = wm.ourPlayer(ch)->pos().y;
                                                sec_unum = ch;
                                        }
                                }

                                for ( int ch = 9; ch <= 11; ch++ )
                                {
                                        if (ch == first_unum || ch == sec_unum)
                                                continue;

                                        if (first_unum > 0 && sec_unum > 0)
                                                third_unum = ch;
                                }

                            }

                        bool first = false;
                        bool sec = false;
                        bool third = false;

			double voron_depth = 42.0;
            // C2D: Helios 18 Tune removed -> replace with BNN

			// if (helios2018)
			// 	voron_depth = 36.0;
			// if (heliosbase)
			// 	voron_depth = 0.2;

                        if ( wm.gameMode().type() == GameMode::PlayOn && wm.ball().pos().x > voron_depth)
                        {
                            if (first_pt.x > -1.0 && first_unum > 0)
                            {
                                first = true;
                                M_positions[first_unum-1] = first_pt;
                            }
                            if (mid_pt.x > -1.0  && sec_unum > 0)
                            {
                                sec = true;
                                M_positions[sec_unum-1] = mid_pt;
                            }
                            if (third_pt.x > -1.0 && third_unum > 0)
                            {
                                third = true;
                                M_positions[third_unum-1] = third_pt;
                            }
                        }
        // end of assignment

        for ( int unum = 1; unum <= 11; ++unum )
        {
            // G2d: skip assigned players

            if ( unum == first_unum && first )
                continue;

            if ( unum == sec_unum && sec )
                continue;

            if ( unum == third_unum && third )
                continue;

            if ( M_positions[unum-1].x > max_x )
            {
                dlog.addText( Logger::TEAM,
                              "____ %d offside. home_pos_x %.2f -> %.2f",
                              unum,
                              M_positions[unum-1].x, max_x );
                M_positions[unum-1].x = max_x;
            }
        }
    }

    int self_min = wm.interceptTable().selfStep();
    int mate_min = wm.interceptTable().teammateStep();
    int opp_min = wm.interceptTable().opponentStep();

    const int our_min = std::min(self_min, mate_min);

    // G2d : wing tactic → O1 (2026-07-05): PLANTILLA DE OCUPACIÓN DEL ÁREA.
    // Trigger ESTRECHO: antes se disparaba desde x=-15 (pleno build-up) y con
    // |y|>7 → 6 jugadores subían en cualquier posesión y por ahí llegaban los
    // contragolpes (46 goles de transición en competencia). Ahora solo con
    // balón claramente ofensivo (x>15) y de verdad en banda (|y|>12).
    double wing_x = 15.0;
    double wing_y = 12.0;
    double wing_depth = 5.0;
    double wing_limit = 39.0;

    // C2D: Tune removed
    // if (mt || helios2018)
    // {
    //     wing_depth = 10.0;
    //     wing_y = 17.0;
    // }

    if (our_min < opp_min)
        if (wm.ball().pos().x > wing_x)
            if (wm.ball().pos().x < wing_limit)
                if (fabs(wm.ball().pos().y) > wing_y)
                    if (!indFK && !dirFK && !cornerK && !kickin)
                    {
                        M_positions[9 - 1].x = wm.offsideLineX() + wm.ball().vel().x;
                        M_positions[10 - 1].x = wm.offsideLineX() + wm.ball().vel().x;
                        M_positions[11 - 1].x = wm.offsideLineX() + wm.ball().vel().x;

                        // O1 (2026-07-05, análisis del equipo): PLANTILLA DE
                        // OCUPACIÓN DEL ÁREA. El desmarque orbita ≤7m del home
                        // (bhv_unmark) → con homes centrales, unmark/3-ring/
                        // generadores trabajan para el centro solos. Datos
                        // competencia: 1.62 compañeros centrales, 8 toques
                        // centrales vs 40 en banda, 2.2 remates/partido.
                        // Ocupación (balón en banda derecha):
                        //   P10 amplitud 28 | P11 punto penal +3 | P9 2º palo -9
                        //   P7 (OH débil) BORDE DEL ÁREA offside-8 para el
                        //   cutback | P8 half-space apoyo | P6 rest-defense
                        //   offside-18 (vigilar GF: knob riesgoso, ver memoria).
                        double midX = wm.offsideLineX() - wing_depth;

                        if (wm.ball().pos().y > 0)
                        {
                            M_positions[10 - 1].y = 28.0;   // extremo der: amplitud
                            M_positions[11 - 1].y = 3.0;    // punta: punto penal
                            M_positions[9 - 1].y = -9.0;    // extremo izq: 2º palo

                            M_positions[8 - 1].x = midX;    // medio der: half-space
                            M_positions[8 - 1].y = 14.0;
                            M_positions[7 - 1].x = wm.offsideLineX() - 8.0; // OH débil:
                            M_positions[7 - 1].y = -4.0;    //   borde del área (cutback)
                            M_positions[6 - 1].x = wm.offsideLineX() - 18.0; // pivote:
                            M_positions[6 - 1].y = 6.0;     //   rest-defense
                        }
                        else
                        {
                            M_positions[9 - 1].y = -28.0;   // extremo izq: amplitud
                            M_positions[11 - 1].y = -3.0;   // punta: punto penal
                            M_positions[10 - 1].y = 9.0;    // extremo der: 2º palo

                            M_positions[7 - 1].x = midX;    // medio izq: half-space
                            M_positions[7 - 1].y = -14.0;
                            M_positions[8 - 1].x = wm.offsideLineX() - 8.0; // OH débil:
                            M_positions[8 - 1].y = 4.0;     //   borde del área (cutback)
                            M_positions[6 - 1].x = wm.offsideLineX() - 18.0; // pivote:
                            M_positions[6 - 1].y = -6.0;    //   rest-defense
                        }
                    }

    M_position_types.clear();
    for ( int unum = 1; unum <= 11; ++unum )
    {
        PositionType type = Position_Center;

        const RoleType role_type = f->roleType( unum );
        if ( role_type.side() == RoleType::Left )
        {
            type = Position_Left;
        }
        else if ( role_type.side() == RoleType::Right )
        {
            type = Position_Right;
        }

        M_position_types.push_back( type );

        dlog.addText( Logger::TEAM,
                      "__ %d home pos (%.2f %.2f) type=%d",
                      unum,
                      M_positions[unum-1].x, M_positions[unum-1].y,
                      type );
        dlog.addCircle( Logger::TEAM,
                        M_positions[unum-1], 0.5,
                        "#000000" );
    }
}


/*-------------------------------------------------------------------*/
/*!

 */
PositionType
Strategy::getPositionType( const int unum ) const
{
    const int number = roleNumber( unum );

    if ( number < 1 || 11 < number )
    {
        std::cerr << __FILE__ << ' ' << __LINE__
                  << ": Illegal number : " << number
                  << std::endl;
        return Position_Center;
    }

    try
    {
        return M_position_types.at( number - 1 );
    }
    catch ( std::exception & e )
    {
        std::cerr<< __FILE__ << ':' << __LINE__ << ':'
                 << " Exception caught! " << e.what()
                 << std::endl;
        return Position_Center;
    }
}

/*-------------------------------------------------------------------*/
/*!

 */
Vector2D
Strategy::getPosition( const int unum ) const
{
    const int number = roleNumber( unum );

    if ( number < 1 || 11 < number )
    {
        std::cerr << __FILE__ << ' ' << __LINE__
                  << ": Illegal number : " << number
                  << std::endl;
        return Vector2D::INVALIDATED;
    }

    try
    {
        return M_positions.at( number - 1 );
    }
    catch ( std::exception & e )
    {
        std::cerr<< __FILE__ << ':' << __LINE__ << ':'
                 << " Exception caught! " << e.what()
                 << std::endl;
        return Vector2D::INVALIDATED;
    }
}

/*-------------------------------------------------------------------*/
/*!

 */
Formation::Ptr
Strategy::getFormation( const WorldModel & wm ) const
{

    //
    // play on
    //
    if ( wm.gameMode().type() == GameMode::PlayOn )
    {
        // Resetear fase de taunt cuando el juego se reanuda.
        if ( M_taunt_phase != -1 )
        {
             // std::cerr << "[DEBUG Strategy] Resetting taunt phase (Game Resumed)" << std::endl;
             M_taunt_phase = -1;
        }

        switch ( M_current_situation ) {
        case Defense_Situation:
            return M_defense_formation;
        case Offense_Situation:
            return M_offense_formation;
        default:
            break;
        }
        return M_normal_formation;
    }

    //
    // kick in, corner kick
    //
    if ( wm.gameMode().type() == GameMode::KickIn_
         || wm.gameMode().type() == GameMode::CornerKick_ )
    {
        if ( wm.ourSide() == wm.gameMode().side() )
        {
            // our kick-in or corner-kick
            return M_kickin_our_formation;
        }
        else
        {
            return M_setplay_opp_formation;
        }
    }

    //
    // our indirect free kick
    //
    if ( ( wm.gameMode().type() == GameMode::BackPass_
           && wm.gameMode().side() == wm.theirSide() )
         || ( wm.gameMode().type() == GameMode::IndFreeKick_
              && wm.gameMode().side() == wm.ourSide() ) )
    {
        return M_indirect_freekick_our_formation;
    }

    //
    // opponent indirect free kick
    //
    if ( ( wm.gameMode().type() == GameMode::BackPass_
           && wm.gameMode().side() == wm.ourSide() )
         || ( wm.gameMode().type() == GameMode::IndFreeKick_
              && wm.gameMode().side() == wm.theirSide() ) )
    {
        return M_indirect_freekick_opp_formation;
    }

    //
    // after foul
    //
    if ( wm.gameMode().type() == GameMode::FoulCharge_
         || wm.gameMode().type() == GameMode::FoulPush_ )
    {
        if ( wm.gameMode().side() == wm.ourSide() )
        {
            //
            // opponent (indirect) free kick
            //
            if ( wm.ball().pos().x < ServerParam::i().ourPenaltyAreaLineX() + 1.0
                 && wm.ball().pos().absY() < ServerParam::i().penaltyAreaHalfWidth() + 1.0 )
            {
                return M_indirect_freekick_opp_formation;
            }
            else
            {
                return M_setplay_opp_formation;
            }
        }
        else
        {
            //
            // our (indirect) free kick
            //
            if ( wm.ball().pos().x > ServerParam::i().theirPenaltyAreaLineX()
                 && wm.ball().pos().absY() < ServerParam::i().penaltyAreaHalfWidth() )
            {
                return M_indirect_freekick_our_formation;
            }
            else
            {
                return M_setplay_our_formation;
            }
        }
    }

    //
    // goal kick
    //
    if ( wm.gameMode().type() == GameMode::GoalKick_ )
    {
        if ( wm.gameMode().side() == wm.ourSide() )
        {
            return M_goal_kick_our_formation;
        }
        else
        {
            return M_goal_kick_opp_formation;
        }
    }

    //
    // goalie catch
    //
    if ( wm.gameMode().type() == GameMode::GoalieCatch_ )
    {
        if ( wm.gameMode().side() == wm.ourSide() )
        {
            return M_goalie_catch_our_formation;
        }
        else
        {
            return M_goalie_catch_opp_formation;
        }
    }

    //
    // before kick off
    //
    if ( wm.gameMode().type() == GameMode::BeforeKickOff )
    {
        return M_before_kick_off_formation;
    }

    //
    // kick off (tras el silbato): mantener la forma compacta del saque.
    // Sin esta rama caía al fallback isOurSetPlay() → setplay-our-formation,
    // que dispersaba a los jugadores lejos de sus posiciones de saque.
    //
    if ( wm.gameMode().type() == GameMode::KickOff_ )
    {
        if ( wm.gameMode().side() == wm.ourSide() )
        {
            return M_before_kick_off_our_formation;
        }
        return M_before_kick_off_formation;
    }

    if ( wm.gameMode().type() == GameMode::AfterGoal_ )
    {  
        // Lógica robusta para evitar parpadeos:
        // Si ya estamos en una fase de taunt activa (>=0), mantenemos la secuencia
        // a menos que confirmemos explícitamente que es gol del oponente (theirSide).
        // Si no estamos en taunt, iniciamos solo si es gol nuestro (ourSide).
        
        bool continue_taunt = false;
        
        if ( M_taunt_phase >= 0 )
        {
            // Sticky logic: Ya iniciamos. NO verificamos side() de nuevo.
            // Mantenemos la secuencia hasta que termine el modo AfterGoal_ (o PlayOn resetee).
            // std::cerr << "[DEBUG AfterGoal] Sticky logic active. Phase=" << M_taunt_phase << std::endl;
            continue_taunt = true;
        }
        else
        {
            // No hemos iniciado, verificar si debemos iniciar.
            // Revertimos a wm.gameMode().side() == wm.ourSide() ya que el usuario reportó
            // que esta condición SÍ activaba la secuencia (aunque se quedaba en bucle).
            // Al combinar esto con la lógica "sticky" y el reset corregido, debería funcionar.
            if ( wm.gameMode().side() == wm.ourSide() )
            {
                // std::cerr << "[DEBUG AfterGoal] ANOTAMOS! Iniciando secuencia de taunts (ourSide)" << std::endl;
                M_taunt_counter = 0;
                M_taunt_phase = 0;
                continue_taunt = true;
            }
            else
            {
                // Gol del oponente o neutral, formación normal
                // std::cerr << "[DEBUG AfterGoal] Gol del oponente o neutral (theirSide), formación normal" << std::endl;
                return M_before_kick_off_formation;
            }
        }

        if ( continue_taunt )
        {
            // Incrementar contador manual
            M_taunt_counter++;
            int elapsed_cycles = M_taunt_counter;
            
            // std::cerr << "[DEBUG AfterGoal] Taunt Status: Phase=" << M_taunt_phase
            //           << " Counter=" << M_taunt_counter << std::endl;

            // Fase 0: Mostrar "R" (0-30 ciclos = 0-3 segundos)
            if ( elapsed_cycles < 15 )
            {
                M_taunt_phase = 0;
                // Usar formación específica para cada lado.
                // LEFT=-1, RIGHT=1.
                // Si somos RIGHT (1), usamos la formación RIGHT (que tiene Y invertida para compensar rotación).
                // std::cerr << "[DEBUG AfterGoal] Fase 0: Mostrar 'R'. My SideID=" << (int)wm.ourSide()
                //           << " LEFT=" << (int)rcsc::LEFT << " RIGHT=" << (int)rcsc::RIGHT << std::endl;
                if ( wm.ourSide() == rcsc::LEFT )
                {
                    // std::cerr << "[DEBUG AfterGoal] Returning R LEFT formation" << std::endl;
                    return M_after_goal_formation;
                }
                else
                {
                    // std::cerr << "[DEBUG AfterGoal] Returning R RIGHT formation" << std::endl;
                    return M_after_goal_r_right_formation;
                }
            }
            // Fase 1: Mostrar "T" (30-60 ciclos = 3-6 segundos)
            else if ( elapsed_cycles < 30 )
            {
                M_taunt_phase = 1;
                // std::cerr << "[DEBUG AfterGoal] Fase 1: Mostrar 'T'. My SideID=" << (int)wm.ourSide() << std::endl;
                if ( wm.ourSide() == rcsc::LEFT )
                {
                    // std::cerr << "[DEBUG AfterGoal] Returning T LEFT formation" << std::endl;
                    return M_after_goal_t_left_formation;
                }
                else
                {
                    // std::cerr << "[DEBUG AfterGoal] Returning T RIGHT formation" << std::endl;
                    return M_after_goal_t_right_formation;
                }
            }
            // Fase 2: Formación normal (después de 60 ciclos = 6 segundos)
            else
            {
                M_taunt_phase = 2;
                // std::cerr << "[DEBUG AfterGoal] Fase 2: Formación normal" << std::endl;
                return M_before_kick_off_formation;
            }
        }
    } 
    if ( wm.gameMode().isOurSetPlay( wm.ourSide() ) )
    {
        return M_setplay_our_formation;
    }

    if ( wm.gameMode().type() != GameMode::PlayOn )
    {
        return M_setplay_opp_formation;
    }

    //
    // unknown
    //
    switch ( M_current_situation ) {
    case Defense_Situation:
        return M_defense_formation;
    case Offense_Situation:
        return M_offense_formation;
    default:
        break;
    }

    return M_normal_formation;
}

/*-------------------------------------------------------------------*/
/*!

 */
Strategy::BallArea
Strategy::get_ball_area( const WorldModel & wm )
{
    int ball_step = 1000;
    ball_step = std::min( ball_step, wm.interceptTable().teammateStep() );
    ball_step = std::min( ball_step, wm.interceptTable().opponentStep() );
    ball_step = std::min( ball_step, wm.interceptTable().selfStep() );

    return get_ball_area( wm.ball().inertiaPoint( ball_step ) );
}

/*-------------------------------------------------------------------*/
/*!

 */
Strategy::BallArea
Strategy::get_ball_area( const Vector2D & ball_pos )
{
    dlog.addLine( Logger::TEAM,
                  52.5, -17.0, -52.5, -17.0,
                  "#999999" );
    dlog.addLine( Logger::TEAM,
                  52.5, 17.0, -52.5, 17.0,
                  "#999999" );
    dlog.addLine( Logger::TEAM,
                  36.0, -34.0, 36.0, 34.0,
                  "#999999" );
    dlog.addLine( Logger::TEAM,
                  -1.0, -34.0, -1.0, 34.0,
                  "#999999" );
    dlog.addLine( Logger::TEAM,
                  -30.0, -17.0, -30.0, 17.0,
                  "#999999" );
    dlog.addLine( Logger::TEAM,
                  //-36.5, -34.0, -36.5, 34.0,
                  -35.5, -34.0, -35.5, 34.0,
                  "#999999" );

    if ( ball_pos.x > 36.0 )
    {
        if ( ball_pos.absY() > 17.0 )
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": get_ball_area: Cross" );
            dlog.addRect( Logger::TEAM,
                          36.0, -34.0, 52.5 - 36.0, 34.0 - 17.0,
                          "#00ff00" );
            dlog.addRect( Logger::TEAM,
                          36.0, 17.0, 52.5 - 36.0, 34.0 - 17.0,
                          "#00ff00" );
            return BA_Cross;
        }
        else
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": get_ball_area: ShootChance" );
            dlog.addRect( Logger::TEAM,
                          36.0, -17.0, 52.5 - 36.0, 34.0,
                          "#00ff00" );
            return BA_ShootChance;
        }
    }
    else if ( ball_pos.x > -1.0 )
    {
        if ( ball_pos.absY() > 17.0 )
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": get_ball_area: DribbleAttack" );
            dlog.addRect( Logger::TEAM,
                          -1.0, -34.0, 36.0 + 1.0, 34.0 - 17.0,
                          "#00ff00" );
            dlog.addRect( Logger::TEAM,
                          -1.0, 17.0, 36.0 + 1.0, 34.0 - 17.0,
                          "#00ff00" );
            return BA_DribbleAttack;
        }
        else
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": get_ball_area: OffMidField" );
            dlog.addRect( Logger::TEAM,
                          -1.0, -17.0, 36.0 + 1.0, 34.0,
                          "#00ff00" );
            return BA_OffMidField;
        }
    }
    else if ( ball_pos.x > -30.0 )
    {
        if ( ball_pos.absY() > 17.0 )
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": get_ball_area: DribbleBlock" );
            dlog.addRect( Logger::TEAM,
                          -30.0, -34.0, -1.0 + 30.0, 34.0 - 17.0,
                          "#00ff00" );
            dlog.addRect( Logger::TEAM,
                          -30.0, 17.0, -1.0 + 30.0, 34.0 - 17.0,
                          "#00ff00" );
            return BA_DribbleBlock;
        }
        else
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": get_ball_area: DefMidField" );
            dlog.addRect( Logger::TEAM,
                          -30.0, -17.0, -1.0 + 30.0, 34.0,
                          "#00ff00" );
            return BA_DefMidField;
        }
    }
    // 2009-06-17 akiyama: -36.5 -> -35.5
    //else if ( ball_pos.x > -36.5 )
    else if ( ball_pos.x > -35.5 )
    {
        if ( ball_pos.absY() > 17.0 )
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": get_ball_area: CrossBlock" );
            dlog.addRect( Logger::TEAM,
                          //-36.5, -34.0, 36.5 - 30.0, 34.0 - 17.0,
                          -35.5, -34.0, 35.5 - 30.0, 34.0 - 17.0,
                          "#00ff00" );
            dlog.addRect( Logger::TEAM,
                          -35.5, 17.0, 35.5 - 30.0, 34.0 - 17.0,
                          "#00ff00" );
            return BA_CrossBlock;
        }
        else
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": get_ball_area: Stopper" );
            dlog.addRect( Logger::TEAM,
                          //-36.5, -17.0, 36.5 - 30.0, 34.0,
                          -35.5, -17.0, 35.5 - 30.0, 34.0,
                          "#00ff00" );
            // 2009-06-17 akiyama: Stopper -> DefMidField
            //return BA_Stopper;
            return BA_DefMidField;
        }
    }
    else
    {
        if ( ball_pos.absY() > 17.0 )
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": get_ball_area: CrossBlock" );
            dlog.addRect( Logger::TEAM,
                          -52.5, -34.0, 52.5 - 36.5, 34.0 - 17.0,
                          "#00ff00" );
            dlog.addRect( Logger::TEAM,
                          -52.5, 17.0, 52.5 - 36.5, 34.0 - 17.0,
                          "#00ff00" );
            return BA_CrossBlock;
        }
        else
        {
            dlog.addText( Logger::TEAM,
                          __FILE__": get_ball_area: Danger" );
            dlog.addRect( Logger::TEAM,
                          -52.5, -17.0, 52.5 - 36.5, 34.0,
                          "#00ff00" );
            return BA_Danger;
        }
    }

    dlog.addText( Logger::TEAM,
                  __FILE__": get_ball_area: unknown area" );
    return BA_None;
}

/*-------------------------------------------------------------------*/
/*!

 */
double
Strategy::get_normal_dash_power( const WorldModel & wm )
{
    static bool s_recover_mode = false;

    // G2d: role
    int role = Strategy::i().roleNumber(wm.self().unum());

    if ( wm.self().staminaModel().capacityIsEmpty() )
    {
        return std::min( ServerParam::i().maxDashPower(),
                         wm.self().stamina() + wm.self().playerType().extraStamina() );
    }

    const int self_min = wm.interceptTable().selfStep();
    const int mate_min = wm.interceptTable().teammateStep();
    const int opp_min = wm.interceptTable().opponentStep();

    // SPEED BOOST when closest to ball — but with stamina conservation
    // Backward dash costs 2x stamina, effort only recovers above 60% stamina.
    // Burning all stamina early leaves the player degraded in late game.
    if ( self_min <= mate_min )
    {
        // Suelo duro: por debajo de recoverDecThr (2400) el recovery se daña
        // PERMANENTEMENTE. El umbral anterior (25% = 2000) ya estaba por
        // debajo y dejaba jugadores arruinados para el resto del partido.
        // Margen +600 para no rozar el umbral; excepción solo intercepción
        // inmediata (self_min <= 3).
        if ( ! wm.self().staminaModel().capacityIsEmpty()
             && wm.self().stamina() < ServerParam::i().recoverDecThrValue() + 600.0
             && self_min > 3 )
        {
            double conservative = wm.self().playerType().staminaIncMax()
                                * wm.self().recovery() * 1.5;
            return std::min( conservative, ServerParam::i().maxDashPower() );
        }
        return ServerParam::i().maxDashPower();
    }

    // check recover
    if ( wm.self().staminaModel().capacityIsEmpty() )
    {
        s_recover_mode = false;
    }
    else if ( wm.self().stamina() < ServerParam::i().staminaMax() * 0.5 )
    {
        s_recover_mode = true;
    }
    else if ( wm.self().stamina() > ServerParam::i().staminaMax() * 0.7 )
    {
        s_recover_mode = false;
    }

    /*--------------------------------------------------------*/
    double dash_power = ServerParam::i().maxDashPower();
    const double my_inc
        = wm.self().playerType().staminaIncMax()
        * wm.self().recovery();

    if ( wm.ourDefenseLineX() > wm.self().pos().x
         && wm.ball().pos().x < wm.ourDefenseLineX() + 20.0 )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": (get_normal_dash_power) correct DF line. keep max power" );
        // keep max power
        dash_power = ServerParam::i().maxDashPower();
    }
    else if ( s_recover_mode )
    {
        dash_power = my_inc - 25.0; // preffered recover value
        if ( dash_power < 0.0 ) dash_power = 0.0;

        dlog.addText( Logger::TEAM,
                      __FILE__": (get_normal_dash_power) recovering" );
    }

    // G2d: run to offside line
    else if (wm.ball().pos().x > 0.0 && wm.self().pos().x < wm.offsideLineX() && fabs(wm.ball().pos().x - wm.self().pos().x) < 25.0)
        dash_power = ServerParam::i().maxDashPower();

    // G2d: defenders
    else if (wm.ball().pos().x < 10.0 && (role == 4 || role == 5 || role == 2 || role == 3))
        dash_power = ServerParam::i().maxDashPower();

    // G2d: midfielders
    else if (wm.ball().pos().x < -10.0 && (role == 6 || role == 7 || role == 8))
        dash_power = ServerParam::i().maxDashPower();

    // G2d: run in opp penalty area
    else if (wm.ball().pos().x > 36.0 && wm.self().pos().x > 36.0 && mate_min < opp_min - 4)
        dash_power = ServerParam::i().maxDashPower();

    // exist kickable teammate — sprint to get open for a pass
    else if ( wm.kickableTeammate()
              && wm.ball().distFromSelf() < 20.0 )
    {
        dash_power = std::min( my_inc * 1.6,
                               ServerParam::i().maxDashPower() );
        dlog.addText( Logger::TEAM,
                      __FILE__": (get_normal_dash_power) exist kickable teammate. dash_power=%.1f",
                      dash_power );
    }
    // in offside area
    else if ( wm.self().pos().x > wm.offsideLineX() )
    {
        dash_power = ServerParam::i().maxDashPower();
        dlog.addText( Logger::TEAM,
                      __FILE__": in offside area. dash_power=%.1f",
                      dash_power );
    }
    else if ( wm.ball().pos().x > 25.0
              && wm.ball().pos().x > wm.self().pos().x + 10.0
              && self_min < opp_min - 6
              && mate_min < opp_min - 6 )
    {
        dash_power = bound( ServerParam::i().maxDashPower() * 0.1,
                            my_inc * 0.5,
                            ServerParam::i().maxDashPower() );
        dlog.addText( Logger::TEAM,
                      __FILE__": (get_normal_dash_power) opponent ball dash_power=%.1f",
                      dash_power );
    }
    // normal — use higher multiplier to keep up with fast opponents
    else
    {
        dash_power = std::min( my_inc * 2.2,
                               ServerParam::i().maxDashPower() );
        dlog.addText( Logger::TEAM,
                      __FILE__": (get_normal_dash_power) normal mode dash_power=%.1f",
                      dash_power );
    }

    return dash_power;
}

/*-------------------------------------------------------------------*/
/*!
  Per-player defensive threshold — adapted from Cyrus isDefenseSituation.
  Each role has a different "dif" based on ball position:
    backs  (2-5): dif=3 in own half  → need 3-cycle advantage to attack
    halves (6-8): dif=2 in own half  → need 2-cycle advantage
    fwds   (9-11): dif=-2            → almost never personally defensive
  Defense if: opp_min - our_min < dif
*/
bool
Strategy::isPersonalDefenseSituation( const WorldModel & wm, int unum ) const
{
    const int self_min = wm.interceptTable().selfStep();
    const int mate_min = wm.interceptTable().teammateStep();
    const int opp_min  = wm.interceptTable().opponentStep();
    const int our_min  = std::min( self_min, mate_min );

    const int role = roleNumber( unum );
    const double ball_x = wm.ball().inertiaPoint(
                              std::min( our_min, opp_min ) ).x;

    int dif = 0;

    if ( role >= 2 && role <= 5 )        // backs
    {
        if      ( ball_x < -20.0 ) dif = 3;
        else if ( ball_x <  20.0 ) dif = 2;
        else if ( ball_x <  40.0 ) dif = 1;
        else                        dif = 0;
    }
    else if ( role >= 6 && role <= 8 )   // halves
    {
        if      ( ball_x < -20.0 ) dif = 2;
        else if ( ball_x <  20.0 ) dif = 2;
        else if ( ball_x <  40.0 ) dif = 0;
        else                        dif = -2;
    }
    else                                 // forwards (9-11)
    {
        dif = -2;
    }

    return ( opp_min - our_min ) < dif;
}

/*-------------------------------------------------------------------*/
/*!
  Dynamic formation shifts applied after f->getPositions().
  Inspired by Cyrus updateFormation523():
    Defense: role 6 (defensive half) drops to back line → compact 5-2-3
    Offense + losing: roles 4-5 (side backs) push forward → attacking 3-4-3
    Deep defense: roles 7-8 (off. halves) compress backward
*/
void
Strategy::applyDynamicFormationShifts( const WorldModel & wm,
                                        const Vector2D & ball_pos )
{
    if ( (int)M_positions.size() < 11 ) return;
    if ( wm.gameMode().type() != GameMode::PlayOn ) return;

    const int our_score = ( wm.ourSide() == LEFT
                            ? wm.gameMode().scoreLeft()
                            : wm.gameMode().scoreRight() );
    const int opp_score = ( wm.ourSide() == LEFT
                            ? wm.gameMode().scoreRight()
                            : wm.gameMode().scoreLeft() );
    const int score_diff = our_score - opp_score;

    const int opp_min  = wm.interceptTable().opponentStep();
    const int mate_min = std::min( wm.interceptTable().teammateStep(),
                                   wm.interceptTable().selfStep() );

    // ── F523: role 6 (idx 5) drops to back line when defending ──────────
    // Condition mirrors Cyrus: ball in own half OR opponent wins ball race by 2+
    if ( ball_pos.x < 15.0 || opp_min < mate_min - 2 )
    {
        // Drop role 6 to be level with the deepest side-back (role 4 or 5)
        const double back_line_x = std::min( M_positions[3].x,
                                              M_positions[4].x );
        if ( M_positions[5].x > back_line_x )
        {
            M_positions[5].x = back_line_x + 1.0; // 1m ahead of back line
            dlog.addText( Logger::TEAM,
                          __FILE__": F523: role6 drops to x=%.1f",
                          M_positions[5].x );
        }
    }

    // ── Offensive push: roles 4-5 (idx 3-4) advance when losing ────────
    // Only when ball is in opponent half AND losing — conservative push
    if ( M_current_situation == Offense_Situation
         && score_diff <= -1
         && ball_pos.x > 10.0 )  // solo cuando el balon ya esta en campo rival
    {
        const double push = 3.0;  // push conservador: 3m max
        M_positions[3].x = std::min( M_positions[3].x + push, 10.0 );
        M_positions[4].x = std::min( M_positions[4].x + push, 10.0 );
        dlog.addText( Logger::TEAM,
                      __FILE__": F523: roles4-5 push forward %.1fm (losing %d)",
                      push, -score_diff );
    }

    // ── Proteger ventaja: ganando en el tramo final → bloque bajo ───────
    // Rama simétrica al push ofensivo: medios comprimen y los laterales
    // no suben, para cerrar el partido sin regalar transiciones.
    {
        const int total_cycles = ServerParam::i().actualHalfTime()
                                 * ServerParam::i().nrNormalHalfs();
        const int remaining    = total_cycles - wm.time().cycle();

        if ( score_diff >= 1 && remaining < 600 && ball_pos.x < 30.0 )
        {
            for ( int i = 5; i <= 7; ++i )  // roles 6-8
            {
                M_positions[i].x = std::max( M_positions[i].x - 4.0, -36.0 );
            }
            // Side backs (roles 4-5) no pasan de -10
            M_positions[3].x = std::min( M_positions[3].x, -10.0 );
            M_positions[4].x = std::min( M_positions[4].x, -10.0 );

            dlog.addText( Logger::TEAM,
                          __FILE__": F523: protect lead (+%d, %d cycles left) → low block",
                          score_diff, remaining );
        }
    }

    // ── Deep defense: roles 7-8 (idx 6-7) compress back ────────────────
    if ( ball_pos.x < -30.0
         && M_current_situation == Defense_Situation )
    {
        const double cb_x = std::min( M_positions[1].x, M_positions[2].x );
        for ( int i = 6; i <= 7; i++ )
        {
            double new_x = std::max( M_positions[i].x - 3.0, cb_x + 4.0 );
            if ( new_x < M_positions[i].x )
            {
                M_positions[i].x = new_x;
            }
        }
        dlog.addText( Logger::TEAM,
                      __FILE__": F523: roles7-8 compress back (ball x=%.1f)",
                      ball_pos.x );
    }
}
