// -*-c++-*-
/*
 * cyrus_interceptable.cpp — port fiel de Cyrus2D OpenSource
 * (src/move_def/cyrus_interceptable.cpp, Nader Zare 2017).
 * Cambios del port (2026-07-03): includes librcsc estándar (el include de
 * body_intercept2009 del original no se usaba), getBestIntercept toma la
 * tabla por referencia y null-guardea firstOpponent, y se añade el helper
 * opponentTrap() (conveniencia RoboTech). La LÓGICA es verbatim.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "cyrus_interceptable.h"

#include <rcsc/player/world_model.h>
#include <rcsc/player/player_object.h>
#include <rcsc/player/intercept_table.h>
#include <rcsc/common/server_param.h>
#include <rcsc/common/player_type.h>
#include <rcsc/common/logger.h>
#include <rcsc/math_util.h>

#include <algorithm>
#include <cmath>

using namespace rcsc;
using namespace std;

CyrusOppInterceptTable::CyrusOppInterceptTable( int cycle,
                                                rcsc::Vector2D current_position,
                                                int turn_cycle, double dist_ball )
{
    this->cycle = cycle;
    this->current_position = current_position;
    this->turn_cycle = turn_cycle;
    this->dist_ball = dist_ball;
}

vector<CyrusOppInterceptTable>
CyrusPlayerIntercept::predict( const PlayerObject & player,
                               const PlayerType & player_type,
                               const int max_cycle ) const
{
    vector<CyrusOppInterceptTable> res;
    const double penalty_x_abs = ServerParam::i().pitchHalfLength()
        - ServerParam::i().penaltyAreaLength();
    const double penalty_y_abs = ServerParam::i().penaltyAreaHalfWidth();

    const int pos_count = std::min( player.seenPosCount(), player.posCount() );
    const Vector2D & player_pos = ( player.seenPosCount() <= player.posCount()
                                    ? player.seenPos()
                                    : player.pos() );
    int min_cycle = 0;
    {
        Vector2D ball_to_player = player_pos - M_world.ball().pos();
        ball_to_player.rotate( -M_world.ball().vel().th() );
        min_cycle = static_cast<int>( std::floor(
                        ball_to_player.absY() / player_type.realSpeedMax() ) );
    }

    if ( player.isTackling() ) {
        min_cycle += std::max( 0,
            ServerParam::i().tackleCycles() - player.tackleCount() - 2 );
    }

    min_cycle = std::max( 0,
        min_cycle - std::min( player.seenPosCount(), player.posCount() ) );

    if ( min_cycle > max_cycle ) {
        return res;
    }

    const std::size_t MAX_LOOP = std::min( static_cast<std::size_t>( max_cycle ),
                                           M_ball_pos_cache.size() );

    for ( std::size_t cycle = static_cast<std::size_t>( min_cycle );
          cycle < MAX_LOOP; ++cycle ) {
        const Vector2D & ball_pos = M_ball_pos_cache.at( cycle );

        const double control_area =
            ( ( player.goalie() && ball_pos.absX() > penalty_x_abs
                && ball_pos.absY() < penalty_y_abs )
              ? ServerParam::i().catchableArea()
              : player_type.kickableArea() );

        if ( control_area + player_type.realSpeedMax() * ( cycle + pos_count )
             + 0.5 < player_pos.dist( ball_pos ) ) {
            // never reach
            continue;
        }

        if ( canReachAfterTurnDash( cycle, player, player_type, control_area,
                                    ball_pos ) ) {
            int n_turn = predictTurnCycle( cycle, player, player_type,
                                           control_area, ball_pos );
            double dist_ball = ( player.pos()
                                 + ( player.pos().polar2vector( cycle,
                                     ( ball_pos - player.pos() ).th() ) ) ).dist( ball_pos );
            CyrusOppInterceptTable tmp( cycle, ball_pos, n_turn, dist_ball );
            res.push_back( tmp );
        }
    }

    return res;
}

bool
CyrusPlayerIntercept::canReachAfterTurnDash( const int cycle,
                                             const PlayerObject & player,
                                             const PlayerType & player_type,
                                             const double & control_area,
                                             const Vector2D & ball_pos ) const
{
    int n_turn = predictTurnCycle( cycle, player, player_type, control_area,
                                   ball_pos );

    int n_dash = cycle - n_turn;
    if ( n_dash < 0 ) {
        return false;
    }

    return canReachAfterDash( n_turn, n_dash, player, player_type, control_area,
                              ball_pos );
}

int
CyrusPlayerIntercept::predictTurnCycle( const int cycle,
                                        const PlayerObject & player,
                                        const PlayerType & player_type,
                                        const double & control_area,
                                        const Vector2D & ball_pos ) const
{
    const Vector2D & ppos = ( player.seenPosCount() <= player.posCount()
                              ? player.seenPos()
                              : player.pos() );
    const Vector2D & pvel = ( player.seenVelCount() <= player.velCount()
                              ? player.seenVel()
                              : player.vel() );

    Vector2D inertia_pos = player_type.inertiaPoint( ppos, pvel, cycle );
    Vector2D target_rel = ball_pos - inertia_pos;
    double target_dist = target_rel.r();
    double turn_margin = 180.0;
    if ( control_area < target_dist ) {
        turn_margin = AngleDeg::asin_deg( control_area / target_dist );
    }
    turn_margin = std::max( turn_margin, 12.0 );

    double angle_diff = ( target_rel.th() - player.body() ).abs();

    if ( target_dist < 5.0 // XXX magic number XXX
         && angle_diff > 90.0 ) {
        // assume back dash
        angle_diff = 180.0 - angle_diff;
    }

    int n_turn = 0;

    double speed = player.vel().r();
    if ( angle_diff > turn_margin ) {
        double max_turn = player_type.effectiveTurn(
            ServerParam::i().maxMoment(), speed );
        angle_diff -= max_turn;
        speed *= player_type.playerDecay();
        ++n_turn;
    }

    return n_turn;
}

bool
CyrusPlayerIntercept::canReachAfterDash( const int n_turn, const int max_dash,
                                         const PlayerObject & player,
                                         const PlayerType & player_type,
                                         const double & control_area,
                                         const Vector2D & ball_pos ) const
{
    const int pos_count = std::min( player.seenPosCount(), player.posCount() );
    const Vector2D & ppos = ( player.seenPosCount() <= player.posCount()
                              ? player.seenPos()
                              : player.pos() );
    const Vector2D & pvel = ( player.seenVelCount() <= player.velCount()
                              ? player.seenVel()
                              : player.vel() );

    Vector2D player_pos = inertia_n_step_point( ppos, pvel, n_turn + max_dash,
                                                player_type.playerDecay() );

    Vector2D player_to_ball = ball_pos - player_pos;
    double player_to_ball_dist = player_to_ball.r();
    player_to_ball_dist -= control_area;

    if ( player_to_ball_dist < 0.0 ) {
        return true;
    }

    int estimate_dash = player_type.cyclesToReachDistance( player_to_ball_dist );
    int n_dash = estimate_dash;
    if ( player.side() != M_world.ourSide() ) {
        n_dash -= bound( 0, pos_count - n_turn,
                         std::min( 6, M_world.ball().seenPosCount() + 1 ) );
    }
    else {
        n_dash -= bound( 0, pos_count - n_turn,
                         std::min( 1, M_world.ball().seenPosCount() ) );
    }

    if ( player.isTackling() ) {
        n_dash += std::max( 0,
            ServerParam::i().tackleCycles() - player.tackleCount() - 2 );
    }

    if ( n_dash <= max_dash ) {
        return true;
    }

    return false;
}

CyrusOppInterceptTable
CyrusPlayerIntercept::getBestIntercept( const WorldModel & wm,
                                        const vector<CyrusOppInterceptTable> & table )
{
    const ServerParam & SP = ServerParam::i();
    const std::vector<CyrusOppInterceptTable> & cache = table;

    if ( cache.empty() || wm.interceptTable().firstOpponent() == nullptr ) {
        CyrusOppInterceptTable tmp = CyrusOppInterceptTable( 1000,
            Vector2D::INVALIDATED, 1000, 1000 );
        return tmp;
    }

    const Vector2D goal_pos( -65.0, 0.0 );
    const double max_pitch_x = ( SP.keepawayMode()
                                 ? SP.keepawayLength() * 0.5 - 1.0
                                 : SP.pitchHalfLength() - 1.0 );
    const double max_pitch_y = ( SP.keepawayMode()
                                 ? SP.keepawayWidth() * 0.5 - 1.0
                                 : SP.pitchHalfWidth() - 1.0 );
    const PlayerObject opp_ball = *( wm.interceptTable().firstOpponent() );
    const double speed_max = opp_ball.playerTypePtr()->realSpeedMax() * 0.9;
    // OJO (semántica de Cyrus): esto evalúa la intercepción DEL RIVAL, así que
    // su "oponente" somos NOSOTROS — por eso usa nuestros steps.
    const int opp_min = min( wm.interceptTable().teammateStep(),
                             wm.interceptTable().selfStep() );

    const CyrusOppInterceptTable * attacker_best = nullptr;
    double attacker_score = 0.0;

    const CyrusOppInterceptTable * forward_best = nullptr;
    double forward_score = 0.0;

    const CyrusOppInterceptTable * noturn_best = nullptr;
    double noturn_score = 10000.0;

    const CyrusOppInterceptTable * nearest_best = nullptr;
    double nearest_score = 10000.0;

    const std::size_t MAX = cache.size();
    for ( std::size_t i = 0; i < MAX; ++i ) {
        const int cycle = cache[i].cycle;
        const Vector2D self_pos = opp_ball.inertiaPoint( cycle );
        Vector2D ball_pos = wm.ball().inertiaPoint( cycle );
        Vector2D ball_vel = wm.ball().vel() * std::pow( SP.ballDecay(), cycle );

        if ( ball_pos.absX() > max_pitch_x || ball_pos.absY() > max_pitch_y ) {
            continue;
        }
        bool attacker = false;
        if ( ball_vel.x < -0.5 && ball_vel.r2() > std::pow( speed_max, 2 )
             && ball_pos.x > -47.0
             && ( ball_pos.x < -35.0 || ball_pos.x < wm.ourDefenseLineX() ) ) {
            attacker = true;
        }

        const double opp_rate = ( attacker ? 0.95 : 0.7 );
        if ( cycle >= opp_min * opp_rate ) {
            continue;
        }

        // attacker type
        if ( attacker ) {
            double goal_dist = 100.0 - std::min( 100.0, ball_pos.dist( goal_pos ) );
            double x_diff = -47.0 + ball_pos.x;

            double score = ( goal_dist / 100.0 )
                * std::exp( -( x_diff * x_diff ) / ( 2.0 * 100.0 ) );
            if ( score > attacker_score ) {
                attacker_best = &cache[i];
                attacker_score = score;
            }
            continue;
        }

        // no turn type
        if ( cache[i].turn_cycle == 0 ) {
            double score = cycle;
            if ( score < noturn_score ) {
                noturn_best = &cache[i];
                noturn_score = score;
            }
            continue;
        }

        // forward type
        if ( ball_vel.x < -0.1 && cycle <= opp_min - 5
             && ball_vel.r2() > std::pow( 0.6, 2 ) ) {
            double score = ( 100.0 * 100.0 )
                - std::min( 100.0 * 100.0, ball_pos.dist2( goal_pos ) );
            if ( score > forward_score ) {
                forward_best = &cache[i];
                forward_score = score;
            }
            continue;
        }

        // other: select nearest one
        {
            double d = self_pos.dist2( ball_pos );
            if ( d < nearest_score ) {
                nearest_best = &cache[i];
                nearest_score = d;
            }
        }
    }

    if ( attacker_best ) {
        return *attacker_best;
    }

    if ( noturn_best && forward_best ) {
        Vector2D noturn_ball_vel = wm.ball().vel()
            * std::pow( SP.ballDecay(), noturn_best->cycle );

        const double noturn_ball_speed = noturn_ball_vel.r();
        if ( noturn_ball_vel.x < -0.1
             && ( noturn_ball_speed > speed_max
                  || noturn_best->cycle <= forward_best->cycle + 2 ) ) {
            return *noturn_best;
        }
    }

    if ( forward_best ) {
        return *forward_best;
    }

    Vector2D fastest_pos = wm.ball().inertiaPoint( cache[0].cycle );
    Vector2D fastest_vel = wm.ball().vel()
        * std::pow( SP.ballDecay(), cache[0].cycle );

    if ( ( fastest_pos.x < 33.0 || fastest_pos.absY() > 20.0 )
         && ( cache[0].cycle >= 10
              || fastest_vel.r() < 1.2 ) ) {
        return cache[0];
    }

    if ( noturn_best && nearest_best ) {
        Vector2D noturn_self_pos = opp_ball.inertiaPoint( noturn_best->cycle );
        Vector2D noturn_ball_pos = wm.ball().inertiaPoint( noturn_best->cycle );
        Vector2D nearest_self_pos = opp_ball.inertiaPoint( nearest_best->cycle );
        Vector2D nearest_ball_pos = wm.ball().inertiaPoint( nearest_best->cycle );

        if ( noturn_self_pos.dist2( noturn_ball_pos )
             < nearest_self_pos.dist2( nearest_ball_pos ) ) {
            return *noturn_best;
        }

        if ( nearest_best->cycle <= noturn_best->cycle + 2 ) {
            Vector2D nearest_ball_vel = wm.ball().vel()
                * std::pow( SP.ballDecay(), nearest_best->cycle );

            const double nearest_ball_speed = nearest_ball_vel.r();
            if ( nearest_ball_speed < 0.7 ) {
                return *nearest_best;
            }

            Vector2D noturn_ball_vel = wm.ball().vel()
                * std::pow( SP.ballDecay(), noturn_best->cycle );

            if ( nearest_best->dist_ball
                 < opp_ball.playerTypePtr()->kickableArea() - 0.4
                 && nearest_best->dist_ball < noturn_best->dist_ball
                 && noturn_ball_vel.x > -0.5
                 && noturn_ball_vel.r2() > std::pow( 1.0, 2 )
                 && noturn_ball_pos.x < nearest_ball_pos.x ) {
                return *nearest_best;
            }

            if ( nearest_ball_speed > 0.7
                 && nearest_self_pos.dist( nearest_ball_pos )
                 < opp_ball.playerTypePtr()->kickableArea() ) {
                return *nearest_best;
            }
        }

        return *noturn_best;
    }

    if ( noturn_best ) {
        return *noturn_best;
    }

    if ( nearest_best ) {
        return *nearest_best;
    }

    if ( opp_ball.pos().x < -40.0 && wm.ball().vel().r() > 1.8
         && wm.ball().vel().th().abs() < 100.0 && cache[0].cycle > 1 ) {
        const CyrusOppInterceptTable * chance_best = nullptr;
        for ( std::size_t i = 0; i < MAX; ++i ) {
            if ( cache[i].cycle <= cache[0].cycle + 3
                 && cache[i].cycle <= opp_min - 2 ) {
                chance_best = &cache[i];
            }
        }

        if ( chance_best ) {
            return *chance_best;
        }
    }

    return cache[0];
}

vector<Vector2D>
CyrusPlayerIntercept::createBallCache( const WorldModel & wm )
{
    const ServerParam & SP = ServerParam::i();
    const std::size_t MAX_CYCLE = 30;

    vector<Vector2D> ball_pos_cache;
    const double pitch_x_max = ( SP.keepawayMode()
                                 ? SP.keepawayLength() * 0.5
                                 : SP.pitchHalfLength() + 5.0 );
    const double pitch_y_max = ( SP.keepawayMode()
                                 ? SP.keepawayWidth() * 0.5
                                 : SP.pitchHalfWidth() + 5.0 );
    const double bdecay = SP.ballDecay();

    Vector2D bpos = wm.ball().pos();
    Vector2D bvel = wm.ball().vel();

    ball_pos_cache.push_back( bpos );

    if ( wm.self().isKickable() ) {
        return ball_pos_cache;
    }

    for ( std::size_t i = 1; i <= MAX_CYCLE; ++i ) {
        bpos += bvel;
        bvel *= bdecay;

        ball_pos_cache.push_back( bpos );

        if ( i >= 5 && bvel.r2() < 0.01 * 0.01 ) {
            break;  // ball stopped
        }

        if ( bpos.absX() > pitch_x_max || bpos.absY() > pitch_y_max ) {
            break;  // out of pitch
        }
    }

    if ( ball_pos_cache.size() == 1 ) {
        ball_pos_cache.push_back( bpos );
    }
    return ball_pos_cache;
}

bool
CyrusPlayerIntercept::opponentTrap( const WorldModel & wm,
                                    Vector2D * trap_pos,
                                    int * trap_cycle )
{
    const PlayerObject * opp = wm.interceptTable().firstOpponent();
    if ( opp == nullptr || opp->unum() < 1 ) {
        return false;
    }

    int cycle = wm.interceptTable().opponentStep();
    Vector2D pos = wm.ball().inertiaPoint( cycle );

    const PlayerType * ptype = opp->playerTypePtr();
    if ( ptype != nullptr ) {
        vector<Vector2D> cache = createBallCache( wm );
        CyrusPlayerIntercept predictor( wm, cache );
        vector<CyrusOppInterceptTable> pred = predictor.predict( *opp, *ptype, 1000 );
        CyrusOppInterceptTable best = getBestIntercept( wm, pred );
        // fallback de Cyrus: predicción absurda → tabla estándar
        if ( best.cycle <= 100 && best.current_position.isValid() ) {
            cycle = best.cycle;
            pos = best.current_position;
        }
    }

    if ( trap_pos )   *trap_pos = pos;
    if ( trap_cycle ) *trap_cycle = cycle;
    return true;
}
