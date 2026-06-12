// -*-c++-*-
// Ported from Cyrus OpenSource move_def/mark_position_finder.cpp
// Adaptations:
//   Setting::i()->mDefenseMove->mMidTh_PosFinderHPosXNegativeTerm  → 5.0
//   Setting::i()->mDefenseMove->mMidTh_PosFinderBackDistXPlusTerm   → 2.0
//   Strategy::i().tmLine(t) == PostLine::back  → roleNumber 2-5

#include "mark_position_finder.h"
#include "strategy.h"
#include <rcsc/player/world_model.h>
#include <rcsc/player/intercept_table.h>
#include <rcsc/player/abstract_player_object.h>
#include <rcsc/common/player_type.h>
#include <rcsc/common/server_param.h>
#include <rcsc/geom/vector_2d.h>
#include <rcsc/geom/angle_deg.h>
#include <algorithm>
#include <cmath>
#include <cstddef>

using namespace rcsc;

// Inline helpers replacing Setting params with tuned constants
namespace {
    inline bool isBack( int unum ) {
        int r = Strategy::i().roleNumber( unum );
        return r >= 2 && r <= 5;
    }
}

Target
MarkPositionFinder::getLeadProjectionMarkTarget( int tmUnum, int oppUnum,
                                                  const WorldModel & wm )
{
    Target target;
    int opp_reach_cycle = wm.interceptTable().opponentStep();
    Vector2D ball_pos = wm.ball().inertiaPoint( opp_reach_cycle );

    if ( wm.getDistTeammateNearestTo( ball_pos, 5 ) > 5.0 ) {
        ball_pos.x -= 4.0;
    }

    Vector2D tm_pos  = wm.ourPlayer( tmUnum )->pos();
    Vector2D opp_pos = wm.theirPlayer( oppUnum )->pos();

    Vector2D best    = Vector2D::polar2vector( 1.0, ( ball_pos - opp_pos ).th() ) + opp_pos;
    double   min_dist = best.dist( tm_pos );

    for ( double d = 2.0; d < opp_pos.dist( ball_pos ); d += 2.0 ) {
        if ( d > 12.0 ) break;
        Vector2D candid = Vector2D::polar2vector( d, ( ball_pos - opp_pos ).th() ) + opp_pos;
        if ( candid.dist( tm_pos ) < min_dist ) {
            min_dist = candid.dist( tm_pos );
            best     = candid;
        }
    }

    target.pos   = best;
    AngleDeg th  = ( ball_pos - opp_pos ).th() + AngleDeg( 90 );
    AngleDeg goal = ( Vector2D( -52, 0 ) - target.pos ).th();

    target.th = ( ( th - goal ).abs() < 90.0 ) ? th : th + AngleDeg( 180 );

    return target;
}

Target
MarkPositionFinder::getLeadNearMarkTarget( int /*tmUnum*/, int oppUnum,
                                            const WorldModel & wm )
{
    Target target;
    const AbstractPlayerObject * opp = wm.theirPlayer( oppUnum );
    int    opp_reach_cycle = wm.interceptTable().opponentStep();
    Vector2D ball_pos      = wm.ball().inertiaPoint( opp_reach_cycle );

    double   dist2opp  = 0.2;
    Vector2D opp_vel   = opp->vel() / 0.4 * 2.0 * opp->playerTypePtr()->playerSpeedMax();
    target.pos         = opp->pos() + opp_vel;
    target.pos        += Vector2D::polar2vector( dist2opp, ( ball_pos - target.pos ).th() );

    return target;
}

Target
MarkPositionFinder::getThMarkTarget( std::size_t /*tmUnum*/, std::size_t oppUnum,
                                      const WorldModel & wm, bool /*debug*/ )
{
    Target target;
    const AbstractPlayerObject * opp = wm.theirPlayer( (int)oppUnum );
    int opp_min = wm.interceptTable().opponentStep();

    Vector2D ball_inertia = wm.ball().inertiaPoint( opp_min );
    Vector2D opp_vel      = opp->vel() / 0.4 * 2.0 * opp->playerTypePtr()->playerSpeedMax();
    if ( opp_vel.x > -1.0 ) opp_vel.x = -1.0;
    target.pos = opp->pos() + opp_vel;
    Vector2D opp_pos = target.pos;

    // librcsc v18 does not expose teammate stamina via AbstractPlayerObject
    const bool   tm_is_tired = false;
    const double tm_tired_x  = 0.0;
    (void)tm_tired_x;

    // mMidTh_PosFinderHPosXNegativeTerm = 5.0
    double tm_def_hpos_x = Strategy::i().getPosition( 2 ).x;
    if ( ball_inertia.x > -15.0 ) {
        tm_def_hpos_x -= 5.0;
    }

    double tm_def_pos_x  = wm.ourDefenseLineX();
    bool   opp_can_pass_now = false;
    if ( ( opp_min <= 3 && ball_inertia.x < tm_def_pos_x + 30.0 )
      || ( opp_min <= 5 && ball_inertia.x < tm_def_pos_x + 20.0 ) ) {
        opp_can_pass_now = true;
    }

    double opp_near_offside_line_x = 1000.0;
    for ( int o = 1; o <= 11; ++o ) {
        const AbstractPlayerObject * opp2 = wm.theirPlayer( o );
        if ( opp2 && opp2->unum() > 0 ) {
            if ( opp2->pos().x < tm_def_pos_x + 3.0
              && opp2->pos().x > tm_def_pos_x - 0.5 ) {
                if ( opp2->pos().x < opp_near_offside_line_x ) {
                    opp_near_offside_line_x = opp2->pos().x;
                }
            }
        }
    }

    auto applyYAdjustment = [&]() {
        double zy = 7.0;
        if ( ball_inertia.x < -17.0 && ball_inertia.absY() < 25.0
          && target.pos.absY() < 15.0
          && std::abs( target.pos.y - ball_inertia.y ) < 20.0 ) {
            zy = 12.0;
        }
        double dist_y = ( ball_inertia.y - target.pos.y ) / zy;
        if ( ball_inertia.y >  20.0 && target.pos.absY() < 15.0 ) dist_y = -1.0;
        if ( ball_inertia.y < -20.0 && target.pos.absY() < 15.0 ) dist_y = +1.0;
        target.pos.y += dist_y;
        if ( target.pos.y < ball_inertia.y )
            target.pos.y = std::min( target.pos.y, ball_inertia.y );
        else
            target.pos.y = std::max( target.pos.y, ball_inertia.y );
    };

    if ( std::abs( tm_def_hpos_x - tm_def_pos_x ) < 2.0 ) {
        // Defense line and home pos coincide — tight marking
        double dist_to_opp = ( opp_min <= 2 ) ? 3.0 : 2.0;

        double dist_backward = 5.0;
        if ( ball_inertia.x > tm_def_pos_x + 15.0 ) {
            dist_backward = ( ( ball_inertia - target.pos ).th().abs() < 30.0 ) ? 2.0 : 3.5;
        } else {
            dist_backward = ( ( ball_inertia - target.pos ).th().abs() < 30.0 ) ? 3.0 : 5.0;
        }
        // mMidTh_PosFinderBackDistXPlusTerm = 2.0
        if ( ball_inertia.x > -25.0 ) {
            dist_backward += 2.0;
        }
        if ( ball_inertia.x < -25.0 && opp_pos.x < -32.5 ) {
            dist_backward = 1.0;
        }

        target.pos.x = std::max(
            std::min( tm_def_hpos_x + 2.0, target.pos.x - dist_to_opp ),
            tm_def_pos_x - dist_backward );

        applyYAdjustment();
    }
    else if ( tm_def_hpos_x > tm_def_pos_x ) {
        // Home pos is in front of defense line
        if ( opp_can_pass_now && opp_near_offside_line_x < 1000.0 ) {
            target.pos.x = opp_near_offside_line_x - 1.0;
        } else {
            target.pos.x = tm_def_pos_x;
            if ( !tm_is_tired || tm_tired_x > tm_def_pos_x - 1.0 ) {
                target.pos.x += ( ball_inertia.x > tm_def_pos_x + 15.0 ) ? 5.0 : 1.5;
            }
        }
        if ( target.pos.x < -36.0 ) {
            target.pos.x = tm_def_pos_x;
            if ( !tm_is_tired || tm_tired_x > tm_def_pos_x - 1.0 ) {
                target.pos.x += ( ball_inertia.x > tm_def_pos_x + 15.0 ) ? 5.0 : 1.5;
            }
        }
        target.pos.x = std::min( target.pos.x, tm_def_hpos_x );
        if ( target.pos.x < -40.0 ) target.pos.x = -40.0;

        applyYAdjustment();
    }
    else {
        // Home pos is behind defense line
        if ( target.pos.x < tm_def_hpos_x ) {
            target.pos.x  = tm_def_hpos_x;
            target.pos.y += std::min( ( ball_inertia.y - target.pos.y ) / 10.0, 3.0 );
        } else if ( target.pos.x < tm_def_pos_x ) {
            target.pos.x = std::max( tm_def_hpos_x, target.pos.x - 2.0 );
        } else {
            target.pos.x = std::min(
                std::max( tm_def_hpos_x, target.pos.x - 2.0 ),
                tm_def_hpos_x + 3.0 );
        }
        applyYAdjustment();
    }

    if ( opp->pos().x > target.pos.x - 3.0 ) {
        target.th = ( opp->pos() - target.pos ).th();
    } else {
        target.th = ( ball_inertia.y > target.pos.y ) ? AngleDeg( 90 ) : AngleDeg( -90 );
    }

    return target;
}

Target
MarkPositionFinder::getDengerMarkTarget( int /*tmUnum*/, int oppUnum,
                                          const WorldModel & wm )
{
    Target target;
    const AbstractPlayerObject * opp = wm.theirPlayer( oppUnum );
    Vector2D ball_inertia = wm.ball().inertiaPoint( wm.interceptTable().opponentStep() );

    if ( std::abs( ball_inertia.y - opp->pos().x ) > 10.0
      || ball_inertia.x - 2.0 > wm.ourDefenseLineX() ) {

        Vector2D opp_pos = opp->pos()
            + Vector2D::polar2vector(
                opp->playerTypePtr()->playerSpeedMax() * ( opp->vel().r() / 0.4 ),
                opp->vel().th() );

        AngleDeg crossAngle = ( ball_inertia - opp_pos ).th();
        target.pos = opp_pos + Vector2D::polar2vector( 1.0, crossAngle );

        if ( ball_inertia.x < target.pos.x )
            target.pos += Vector2D( -1.0, 0.0 );
        else
            target.pos += Vector2D( -0.2, 0.0 );

        target.th = ( ball_inertia - target.pos ).th();
    } else {
        Vector2D opp_pos = opp->pos();

        auto goalie = wm.getOurGoalie();
        if ( goalie ) {
            Vector2D goal_post_left ( -ServerParam::i().pitchHalfLength(),
                                      -ServerParam::i().goalHalfWidth() + 1.0 );
            Vector2D goal_post_right( -ServerParam::i().pitchHalfLength(),
                                       ServerParam::i().goalHalfWidth() - 1.0 );
            Vector2D & danger_goal_post =
                goal_post_left.dist( goalie->pos() ) > goal_post_right.dist( goalie->pos() )
                    ? goal_post_left : goal_post_right;

            target.pos = opp_pos
                + Vector2D::polar2vector( 1.0, ( danger_goal_post - opp_pos ).th() );
        } else {
            Vector2D opp_vel = opp->vel() / 0.4 * 2.0
                               * opp->playerTypePtr()->playerSpeedMax();
            target.pos = opp_pos + opp_vel - Vector2D( 0.5, 0.0 );
        }
        target.th = ( target.pos - Vector2D( -52.0, target.pos.y ) ).th();
    }

    return target;
}
