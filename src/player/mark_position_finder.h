// -*-c++-*-
#ifndef MARK_POSITION_FINDER_H
#define MARK_POSITION_FINDER_H

#include "mark_types.h"

namespace rcsc { class WorldModel; }

class MarkPositionFinder {
public:
    static Target getLeadProjectionMarkTarget( int tmUnum, int oppUnum,
                                               const rcsc::WorldModel & wm );

    static Target getLeadNearMarkTarget( int tmUnum, int oppUnum,
                                         const rcsc::WorldModel & wm );

    static Target getThMarkTarget( std::size_t tmUnum, std::size_t oppUnum,
                                   const rcsc::WorldModel & wm,
                                   bool debug = false );

    static Target getDengerMarkTarget( int tmUnum, int oppUnum,
                                       const rcsc::WorldModel & wm );
};

#endif // MARK_POSITION_FINDER_H
