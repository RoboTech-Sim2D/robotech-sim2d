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

#ifndef BHV_BASIC_TACKLE_H
#define BHV_BASIC_TACKLE_H

#include <rcsc/player/soccer_action.h>
#include <rcsc/geom/vector_2d.h>

namespace rcsc {
class WorldModel;
}

class Bhv_BasicTackle
    : public rcsc::SoccerBehavior {
private:
    const double M_min_probability;
    const double M_body_thr;
public:
    Bhv_BasicTackle( const double & min_prob,
                     const double & body_thr = 90.0 )
        : M_min_probability( min_prob )
        , M_body_thr( body_thr )
      { }

    bool execute( rcsc::PlayerAgent * agent );

    // Port Cyrus (2026-07-05, para el portero migrado): umbral de tackle
    // DINÁMICO según el peligro real del estado (balón entrando a nuestro
    // arco → 0.1; despeje comprometido → 0.3-0.4; juego normal → 0.8-0.9).
    static double calc_takle_prob( const rcsc::WorldModel & wm,
                                   rcsc::Vector2D bp = rcsc::Vector2D::INVALIDATED );

private:

    bool executeOld( rcsc::PlayerAgent * agent );
    bool executeV12( rcsc::PlayerAgent * agent );
    bool executeV14( rcsc::PlayerAgent * agent,
                     const bool use_foul );
};

#endif
