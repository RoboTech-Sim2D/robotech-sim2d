//
// Created by nader on 2022-04-05.
//

#ifndef BHV_BASIC_BLOCK_H
#define BHV_BASIC_BLOCK_H

#include <rcsc/geom/vector_2d.h>
#include <rcsc/player/soccer_action.h>
#include <vector>

namespace rcsc { class WorldModel; }

class Bhv_BasicBlock
    : public rcsc::SoccerBehavior
{
public:
    Bhv_BasicBlock() {}

    bool execute( rcsc::PlayerAgent * agent );

    // Returns the unum of the player that should block (0 = nobody).
    // Read-only: does not execute any action.
    static int who_is_blocker( const rcsc::WorldModel & wm );

private:
    static std::vector<int> get_blockers( const rcsc::WorldModel & wm );
    static std::pair<int, rcsc::Vector2D> get_best_blocker( const rcsc::WorldModel & wm,
                                                             std::vector<int> & tm_blockers );
    static rcsc::AngleDeg dribble_direction_detector( rcsc::Vector2D dribble_pos );

    static int       last_block_cycle;
    static rcsc::Vector2D last_block_pos;
};

#endif
