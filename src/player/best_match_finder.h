// -*-c++-*-
// Ported from Cyrus OpenSource move_def/best_match_finder.h
#ifndef BEST_MATCH_FINDER_H
#define BEST_MATCH_FINDER_H

#include <vector>
#include <utility>

class BestMatchFinder {
public:
    static void fun( std::vector< std::vector<int> > & tasks_best_agents,
                     std::vector<std::size_t>        & sorted_tasks,
                     std::vector<int>                  actions,
                     int                               pointer,
                     double                            mark_eval[12][12],
                     double                          & best_actions_cost,
                     std::vector<int>                & best_actions );

    static std::pair< std::vector<int>, double >
    find_best_dec( double                     mark_eval[12][12],
                   std::vector<std::size_t>   agents,
                   std::vector<std::size_t>   tasks );
};

#endif // BEST_MATCH_FINDER_H
