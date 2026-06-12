// -*-c++-*-
#ifndef MARK_TYPES_H
#define MARK_TYPES_H

#include <rcsc/geom/vector_2d.h>
#include <rcsc/geom/angle_deg.h>
#include <utility>
#include <cstddef>

enum class MarkType {
    NoType              = 0,
    LeadProjectionMark  = 1,
    LeadNearMark        = 2,
    ThMark              = 3,
    ThMarkFastestOpp    = 4,
    ThMarkFar           = 5,
    DangerMark          = 6,
    Block               = 7,
    Goal_keep           = 8,
};

enum class MarkDec {
    NoDec     = 0,
    AntiDef   = 1,
    MidMark   = 2,
    GoalMark  = 3,
    JustBlock = 4,
};

struct Target {
    rcsc::Vector2D pos;
    rcsc::AngleDeg th = rcsc::AngleDeg(1000);
};

typedef std::pair<std::size_t, double> UnumEval;

#endif // MARK_TYPES_H
