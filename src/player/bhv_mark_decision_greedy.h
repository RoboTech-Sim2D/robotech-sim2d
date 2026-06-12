// -*-c++-*-
// Ported from Cyrus OpenSource move_def/bhv_mark_decision_greedy.h
// Adaptations: removed Setting/PostLine/audio/bhv_block dependencies.
// midMarkDecision / goalMarkDecision implemented self-contained using
// MarkPositionFinder + BestMatchFinder (no bhv_mark_decisions.cpp needed).

#ifndef BHV_MARK_DECISION_GREEDY_H
#define BHV_MARK_DECISION_GREEDY_H

#include "mark_types.h"
#include <rcsc/geom/vector_2d.h>
#include <vector>
#include <utility>
#include <cstddef>

namespace rcsc {
class WorldModel;
class PlayerAgent;
}

class BhvMarkDecisionGreedy {
public:
    struct MarkAction {
        std::size_t opp;
        MarkType    type;
    };

    static bool                       use_home_pos;
    static std::pair<long, int>       last_mark;

    BhvMarkDecisionGreedy()  = default;
    ~BhvMarkDecisionGreedy() = default;

    void getMarkTargets( rcsc::PlayerAgent      * agent,
                         MarkType               & mark_type,
                         int                    & mark_unum,
                         bool                   & blocked,
                         std::vector<MarkType>  & global_how_mark,
                         std::vector<std::size_t> & global_tm_mark_target,
                         std::vector<std::size_t> & global_opp_marker );

    static MarkDec markDecision( const rcsc::WorldModel & wm );

    static std::vector<std::size_t> getOppOffensive( const rcsc::WorldModel & wm,
                                                      bool & fastest_opp_marked );

    static void midMarkDecision( rcsc::PlayerAgent      * agent,
                                 MarkType               & mark_type,
                                 int                    & mark_unum,
                                 bool                   & blocked,
                                 std::vector<MarkType>  & global_how_mark,
                                 std::vector<std::size_t> & global_tm_mark_target,
                                 std::vector<std::size_t> & global_opp_marker );

    static void goalMarkDecision( rcsc::PlayerAgent      * agent,
                                  MarkType               & mark_type,
                                  int                    & mark_unum,
                                  bool                   & blocked,
                                  std::vector<MarkType>  & global_how_mark,
                                  std::vector<std::size_t> & global_tm_mark_target,
                                  std::vector<std::size_t> & global_opp_marker );

    static void antiDefMarkDecision( const rcsc::WorldModel & wm,
                                     MarkType               & mark_type,
                                     int                    & mark_unum,
                                     bool                   & blocked,
                                     std::vector<MarkType>  & global_how_mark,
                                     std::vector<std::size_t> & global_tm_mark_target,
                                     std::vector<std::size_t> & global_opp_marker );
};

#endif // BHV_MARK_DECISION_GREEDY_H
