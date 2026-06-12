// -*-c++-*-
// Ported from Cyrus OpenSource move_def/bhv_mark_execute.h
// Phase 4: executes the per-player mark action chosen by BhvMarkDecisionGreedy.
// bhv_block dependency removed (Phase 5). IntentionMark removed (static var approach).

#ifndef BHV_MARK_EXECUTE_H
#define BHV_MARK_EXECUTE_H

#include "mark_types.h"
#include <rcsc/geom/vector_2d.h>

namespace rcsc {
class PlayerAgent;
class WorldModel;
class AbstractPlayerObject;
}

class bhv_mark_execute {
public:
    bhv_mark_execute()  = default;
    ~bhv_mark_execute() = default;

    bool execute( rcsc::PlayerAgent * agent );

    bool run_mark( rcsc::PlayerAgent * agent, int mark_unum, MarkType mark_type );

    void set_mark_target_thr( const rcsc::WorldModel          & wm,
                              const rcsc::AbstractPlayerObject * opp,
                              MarkType                           mark_type,
                              Target                           & target,
                              double                           & dist_thr );

    bool do_move_mark( rcsc::PlayerAgent * agent, Target target,
                       double dist_thr, MarkType mark_type, int opp_unum );

    double th_mark_power  ( rcsc::PlayerAgent * agent,
                            rcsc::Vector2D opp_pos, rcsc::Vector2D target_pos );
    void   th_mark_move   ( rcsc::PlayerAgent * agent, Target targ,
                            double dash_power, double dist_thr, int opp_unum );

    double lead_mark_power( rcsc::PlayerAgent * agent,
                            rcsc::Vector2D opp_pos, rcsc::Vector2D target_pos );
    void   lead_mark_move ( rcsc::PlayerAgent * agent, Target targ,
                            double dash_power, double dist_thr,
                            MarkType mark_type, rcsc::Vector2D opp_pos );

    double other_mark_power( rcsc::PlayerAgent * agent,
                             rcsc::Vector2D opp_pos, rcsc::Vector2D target_pos );
    void   other_mark_move ( rcsc::PlayerAgent * agent, Target targ,
                             double dash_power, double dist_thr );

    bool do_tackle       ( rcsc::PlayerAgent * agent );
    bool back_to_def     ( rcsc::PlayerAgent * agent );
    bool defenseBeInBack ( rcsc::PlayerAgent * agent );
    bool defenseGoBack   ( rcsc::PlayerAgent * agent );

    rcsc::Vector2D change_position_set_play( const rcsc::WorldModel & wm,
                                             rcsc::Vector2D target );
};

#endif // BHV_MARK_EXECUTE_H
