// -*-c++-*-
// Phase 2: Cyrus-style defensive orchestrator
// Calls Bhv_MarkOpponent (Phase 2 placeholder) → replaced by bhv_mark_execute in Phase 4.
// Falls back to smart home-position movement when marking is not triggered.

#ifndef BHV_DEFENSIVE_MOVE_H
#define BHV_DEFENSIVE_MOVE_H

namespace rcsc { class PlayerAgent; }

class Bhv_DefensiveMove {
public:
    bool execute( rcsc::PlayerAgent * agent );
};

#endif // BHV_DEFENSIVE_MOVE_H
