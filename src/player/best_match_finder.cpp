// -*-c++-*-
// Ported from Cyrus OpenSource move_def/best_match_finder.cpp
// Minimal adaptation: removed debug log calls only.

#include "best_match_finder.h"
#include <algorithm>
#include <cstddef>

using namespace std;

void BestMatchFinder::fun( vector< vector<int> > & tasks_best_agents,
                            vector<size_t>        & sorted_tasks,
                            vector<int>             actions,
                            int                     pointer,
                            double                  mark_eval[12][12],
                            double                & best_actions_cost,
                            vector<int>           & best_actions )
{
    double actions_cost         = 0.0;
    double actions_cost_pointer = 0.0;

    for ( int a = 0; a < (int)actions.size(); ++a ) {
        double e = ( actions[a] != 0 )
                   ? mark_eval[ sorted_tasks[a] ][ actions[a] ]
                   : 100.0 * ( (int)actions.size() - a );
        actions_cost += e;
    }
    for ( int a = 0; a < pointer; ++a ) {
        double e = ( actions[a] != 0 )
                   ? mark_eval[ sorted_tasks[a] ][ actions[a] ]
                   : 100.0 * ( (int)actions.size() - a );
        actions_cost_pointer += e;
    }

    if ( actions_cost < best_actions_cost ) {
        best_actions_cost = actions_cost;
        best_actions      = actions;
    }

    if ( actions_cost_pointer < best_actions_cost
      && pointer < (int)actions.size() ) {
        for ( int a = 0; a < (int)tasks_best_agents[pointer].size(); ++a ) {
            int agent = tasks_best_agents[pointer][a];
            if ( agent != 0 ) {
                bool agent_used = false;
                for ( int i = 0; i < (int)actions.size(); ++i ) {
                    if ( agent == actions[i] ) { agent_used = true; break; }
                }
                if ( agent_used ) continue;
            }
            actions[pointer] = agent;
            fun( tasks_best_agents, sorted_tasks, actions, pointer + 1,
                 mark_eval, best_actions_cost, best_actions );
            actions[pointer] = 0;
        }
    }
}


pair< vector<int>, double >
BestMatchFinder::find_best_dec( double           mark_eval[12][12],
                                 vector<size_t>   agents,
                                 vector<size_t>   tasks )
{
    if ( tasks.empty() || agents.empty() )
        return make_pair( vector<int>(), 100000.0 );

    vector< vector<int> > tasks_best_agents;

    for ( size_t o = 0; o < tasks.size(); ++o ) {
        int task = (int)tasks[o];
        tasks_best_agents.push_back( vector<int>() );

        vector< pair<double, int> > task_costs;
        for ( int agent = 1; agent <= 11; ++agent ) {
            if ( find( agents.begin(), agents.end(), (size_t)agent ) != agents.end() )
                task_costs.push_back( { mark_eval[task][agent], agent } );
        }
        sort( task_costs.begin(), task_costs.end() );

        int limit = (int)min( (size_t)3, task_costs.size() );
        for ( int i = 0; i < limit; ++i ) {
            if ( task_costs[i].first >= 1000.0 ) break;
            tasks_best_agents.back().push_back( task_costs[i].second );
            if ( i == (int)task_costs.size() - 1 ) break;
        }
        tasks_best_agents.back().push_back( 0 );
    }

    vector<int> action( tasks_best_agents.size(), 0 );
    vector<int> best_actions( tasks_best_agents.size(), 0 );
    double best_actions_cost = 10000000.0;
    fun( tasks_best_agents, tasks, action, 0, mark_eval, best_actions_cost, best_actions );

    return make_pair( best_actions, best_actions_cost );
}
