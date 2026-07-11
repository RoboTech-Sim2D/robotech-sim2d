// -*-c++-*-

/*!
  \file strategy.h
  \brief team strategy manager Header File
*/

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

#ifndef STRATEGY_H
#define STRATEGY_H

#include "soccer_role.h"

#include <rcsc/formation/formation.h>
#include <rcsc/geom/vector_2d.h>
#include <rcsc/game_time.h>

#include <boost/shared_ptr.hpp>
#include <map>
#include <vector>
#include <string>

// # define USE_GENERIC_FACTORY 1

namespace rcsc {
class CmdLineParser;
class WorldModel;
}

// Key parameters for the ported Cyrus defensive system.
// Start with the calibrated constants; tune after match data is collected.
struct DefenseMoveSetting {
    double midTh_posFinderHPosXNegativeTerm { 5.0 };
    double midTh_posFinderBackDistXPlusTerm { 2.0 };
    double antiDefThreshold                 { 2.0 };
    double midMarkThreshold                 { 1.5 };
    double goalMarkThreshold                { 0.5 };
    double dangerMarkRadius                 { 15.0 };
    double throughPassDangerDist            { 12.0 };
    double blockMinPowerRate                { 0.3  };
};

enum PositionType {
    Position_Left = -1,
    Position_Center = 0,
    Position_Right = 1,
};

enum SituationType {
    Normal_Situation,
    Offense_Situation,
    Defense_Situation,
    OurSetPlay_Situation,
    OppSetPlay_Situation,
    PenaltyKick_Situation,
};

// Compat mínima con la API de Cyrus (para piezas portadas selectivamente,
// hoy: body_intercept_plan). Mapea por roleNumber: 1 GK, 2-5 back, 6-8 half.
enum class PostLine {
    golie,
    back,
    half,
    forward
};

class Strategy {
public:
    PostLine tmLine( size_t unum ) const;

    static const std::string BEFORE_KICK_OFF_CONF;
    static const std::string BEFORE_KICK_OFF_OUR_CONF;
    static const std::string NORMAL_FORMATION_CONF;
    static const std::string DEFENSE_FORMATION_CONF;
    static const std::string OFFENSE_FORMATION_CONF;
    static const std::string GOAL_KICK_OPP_FORMATION_CONF;
    static const std::string GOAL_KICK_OUR_FORMATION_CONF;
    static const std::string GOALIE_CATCH_OPP_FORMATION_CONF;
    static const std::string GOALIE_CATCH_OUR_FORMATION_CONF;
    static const std::string KICKIN_OUR_FORMATION_CONF;
    static const std::string SETPLAY_OPP_FORMATION_CONF;
    static const std::string SETPLAY_OUR_FORMATION_CONF;
    static const std::string INDIRECT_FREEKICK_OPP_FORMATION_CONF;
    static const std::string INDIRECT_FREEKICK_OUR_FORMATION_CONF;
    static const std::string AFTER_GOAL_FORMATION_CONF_Left;
    static const std::string AFTER_GOAL_FORMATION_CONF_Right;
    static const std::string AFTER_GOAL_FORMATION_CONF_T_Left;
    static const std::string AFTER_GOAL_FORMATION_CONF_T_Right;

    enum BallArea {
        BA_CrossBlock, BA_DribbleBlock, BA_DribbleAttack, BA_Cross,
        BA_Stopper,    BA_DefMidField,  BA_OffMidField,   BA_ShootChance,
        BA_Danger,

        BA_None
    };

    static const DefenseMoveSetting & defenseSetting()
      {
          static const DefenseMoveSetting s;
          return s;
      }

private:
    //
    // factories
    //
#ifndef USE_GENERIC_FACTORY
    typedef std::map< std::string, SoccerRole::Creator > RoleFactory;

    RoleFactory M_role_factory;
#endif


    //
    // formations
    //

    rcsc::Formation::Ptr M_before_kick_off_formation;
    rcsc::Formation::Ptr M_before_kick_off_our_formation;

    rcsc::Formation::Ptr M_normal_formation;
    rcsc::Formation::Ptr M_defense_formation;
    rcsc::Formation::Ptr M_offense_formation;

    rcsc::Formation::Ptr M_goal_kick_opp_formation;
    rcsc::Formation::Ptr M_goal_kick_our_formation;
    rcsc::Formation::Ptr M_goalie_catch_opp_formation;
    rcsc::Formation::Ptr M_goalie_catch_our_formation;
    rcsc::Formation::Ptr M_kickin_our_formation;
    rcsc::Formation::Ptr M_setplay_opp_formation;
    rcsc::Formation::Ptr M_setplay_our_formation;
    rcsc::Formation::Ptr M_indirect_freekick_opp_formation;
    rcsc::Formation::Ptr M_indirect_freekick_our_formation;
    rcsc::Formation::Ptr M_after_goal_formation;

    // Formaciones para secuencia de taunts R → T
    rcsc::Formation::Ptr M_after_goal_r_left_formation;
    rcsc::Formation::Ptr M_after_goal_r_right_formation;
    rcsc::Formation::Ptr M_after_goal_t_left_formation;
    rcsc::Formation::Ptr M_after_goal_t_right_formation;

    // Variables para controlar la secuencia de taunts
    mutable int M_taunt_phase;  // -1=inactivo, 0=R, 1=T, 2=normal
    mutable int M_taunt_counter; // Contador manual para la animación



    int M_goalie_unum;


    // situation type
    SituationType M_current_situation;

    // role assignment
    std::vector< int > M_role_number;

    // current home positions
    std::vector< PositionType > M_position_types;
    std::vector< rcsc::Vector2D > M_positions;

    // private for singleton
    Strategy();

    // not used
    Strategy( const Strategy & );
    const Strategy & operator=( const Strategy & );
public:

    static
    Strategy & instance();

    static
    const
    Strategy & i()
      {
          return instance();
      }

    //
    // initialization
    //

    bool init( rcsc::CmdLineParser & cmd_parser );
    bool read( const std::string & config_dir );


    //
    // update
    //

    void update( const rcsc::WorldModel & wm );


    void exchangeRole( const int unum0,
                       const int unum1 );

    //
    // accessor to the current information
    //

    int goalieUnum() const { return M_goalie_unum; }

    int roleNumber( const int unum ) const
      {
          if ( unum < 1 || 11 < unum ) return unum;
          return M_role_number[unum - 1];
      }

    bool isMarkerType( const int unum ) const;

    // Per-player defensive threshold (adapted from Cyrus isDefenseSituation)
    // Returns true if THIS player should adopt a defensive posture based on
    // their role and the current ball position — independent of team situation.
    bool isPersonalDefenseSituation( const rcsc::WorldModel & wm,
                                      int unum ) const;

    SoccerRole::Ptr createRole( const int unum,
                                const rcsc::WorldModel & wm ) const;
    PositionType getPositionType( const int unum ) const;
    rcsc::Vector2D getPosition( const int unum ) const;


private:
    void updateSituation( const rcsc::WorldModel & wm );
    // update the current position table
    void updatePosition( const rcsc::WorldModel & wm );
    // dynamic formation shifts applied on top of base formation (F523 style)
    void applyDynamicFormationShifts( const rcsc::WorldModel & wm,
                                       const rcsc::Vector2D & ball_pos );

    rcsc::Formation::Ptr createFormation( const std::string & filepath );

    rcsc::Formation::Ptr getFormation( const rcsc::WorldModel & wm ) const;

public:
    static
    BallArea get_ball_area( const rcsc::WorldModel & wm );
    static
    BallArea get_ball_area( const rcsc::Vector2D & ball_pos );

    static
    double get_normal_dash_power( const rcsc::WorldModel & wm );
};

#endif
