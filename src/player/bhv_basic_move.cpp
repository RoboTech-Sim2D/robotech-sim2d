// -*-c++-*-

/*
 *Copyright:

 Cyrus2D
 Modified by Omid Amini, Nader Zare
 
 Gliders2d
 Modified by Mikhail Prokopenko, Peter Wang

 Copyright (C) Hidehisa AKIYAMA

 This code is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3, or (at your option)
 any later version.

 This code is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this code; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *EndCopyright:
 */

/////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "bhv_basic_move.h"
#include "strategy.h"
#include "bhv_basic_tackle.h"
#include "neck_offensive_intercept_neck.h"
#include "bhv_basic_block.h"

#include "basic_actions/basic_actions.h"
#include "basic_actions/body_go_to_point.h"
#include "basic_actions/arm_point_to_point.h"
#include "basic_actions/body_intercept.h"
#include "basic_actions/neck_turn_to_ball_or_scan.h"
#include "basic_actions/neck_scan_field.h"
#include "basic_actions/neck_turn_to_low_conf_teammate.h"

#include <rcsc/player/player_agent.h>
#include <rcsc/player/debug_client.h>
#include <rcsc/player/intercept_table.h>
#include <rcsc/player/say_message_builder.h>

#include <rcsc/common/logger.h>
#include <rcsc/common/server_param.h>

#include "neck_offensive_intercept_neck.h"
#include <rcsc/player/soccer_intention.h>
#include "bhv_unmark.h"

using namespace rcsc;

/*-------------------------------------------------------------------*/
/*!

 */
bool
Bhv_BasicMove::execute( PlayerAgent * agent )
{
    dlog.addText( Logger::TEAM,
                  __FILE__": Bhv_BasicMove" );

    const WorldModel & wm = agent->world();

    //-----------------------------------------------
    // tackle
    // Zone-based tackle threshold: failed tackle freezes player 10 cycles.
    // Near own goal that's catastrophic; in opponent half it's low risk.
    double doTackleProb;
    if ( wm.ball().pos().x < -36.0 )
        doTackleProb = 0.85;    // danger zone: only high-confidence tackles
    else if ( wm.ball().pos().x < 0.0 )
        doTackleProb = 0.5;     // defensive half: moderate threshold
    else
        doTackleProb = 0.15;    // offensive half: aggressive attempts

    // With defensive cover behind us, we can afford more risk
    if ( doTackleProb > 0.15 )
    {
        for ( const PlayerObject * tm : wm.teammatesFromSelf() )
        {
            if ( tm && ! tm->goalie()
                 && tm->pos().x < wm.self().pos().x - 3.0
                 && tm->pos().dist( wm.ball().pos() ) < 15.0 )
            {
                doTackleProb = std::max( 0.3, doTackleProb - 0.2 );
                break;
            }
        }
    }

    if ( Bhv_BasicTackle( doTackleProb, 100.0 ).execute( agent ) )
    {
        return true;
    }

    /*--------------------------------------------------------*/
    // chase ball
    const int self_min = wm.interceptTable().selfStep();
    const int mate_min = wm.interceptTable().teammateStep();
    const int opp_min = wm.interceptTable().opponentStep();

    Vector2D target_point = Strategy::i().getPosition( wm.self().unum() );

    // G2d: to retrieve opp team name
    // C2D: Helios 18 Tune removed -> replace with BNN
    // bool helios2018 = false;
    // if (wm.opponentTeamName().find("HELIOS2018") != std::string::npos)
	// helios2018 = true;
//    if (std::min(self_min, mate_min) < opp_min){
//
//    }else{
//        if (Bhv_BasicBlock().execute(agent)){
//            return true;
//        }
//    }
    // G2d: role
    int role = Strategy::i().roleNumber( wm.self().unum() );

    // G2D: blocking

    Vector2D ball = wm.ball().pos();

    double block_d = 10.0; // DEFENSE MOD: Start blocking earlier (was -10.0)

    Vector2D me = wm.self().pos();
    Vector2D homePos = target_point;
    int num = role;

    auto opps = wm.opponentsFromBall();
    const PlayerObject * nearest_opp
            = ( opps.empty()
                ? static_cast< PlayerObject * >( 0 )
                : opps.front() );
    const double nearest_opp_dist = ( nearest_opp
                                      ? nearest_opp->distFromSelf()
                                      : 1000.0 );
    if (ball.x < block_d)
    {
        double block_line = -38.0;

    //  if (helios2018)
    //      block_line = -48.0;

    // acknowledgement: fragments of Marlik-2012 code
        if( (num == 2 || num == 3) && homePos.x < block_line &&
            !( num == 2 && ball.x < -46.0 && ball.y > -18.0 && ball.y < -6.0 &&
               opp_min <= 3 && opp_min <= mate_min && ball.dist(me) < 9.5 ) &&
            !( num == 3 && ball.x < -46.0 && ball.y <  18.0 && ball.y >  6.0  &&
               opp_min <= 3 && opp_min <= mate_min && ball.dist(me) < 9.5 ) ) // do not block in this situation
        {
            // do nothing
        }
        // DEFENSE MOD: Removed "passive wide defense" for CBs (num 2, 3)
        // previously they ignored balls with |y| > 22.0
         else if (Bhv_BasicBlock().execute(agent)){
            return true;
        }

    } // end of block


    // G2d: pressin
    int pressing = 25; //aggressive pressing (was 13)

    if ( role >= 6 && role <= 8 && wm.ball().pos().x > -30.0 && wm.self().pos().x < 10.0 )
        pressing = 7;

    if (fabs(wm.ball().pos().y) > 22.0 && wm.ball().pos().x < 0.0 && wm.ball().pos().x > -36.5 && (role == 4 || role == 5) ) 
        pressing = 23;

    // C2D: Helios 18 Tune removed -> replace with BNN
    // if (helios2018)
	// pressing = 4;

    // Stamina gate (HELIOS 2023 + YuShan 2024):
    // YuShan found midfielders exhaust stamina 300-500 cycles before halftime.
    // Two tiers: below 55% reduce pressing slightly; below 35% halve it.
    {
        double stamina_ratio = wm.self().stamina() / ServerParam::i().staminaMax();
        if ( stamina_ratio < 0.35 )
            pressing = std::max( 3, pressing / 2 );
        else if ( stamina_ratio < 0.55 )
            pressing = std::max( 4, pressing * 3 / 4 );
    }

    // Defensores en campo propio: presionar sin requerir ser el más cercano
    bool defender_deep_press = ( ( role == 2 || role == 3 || role == 4 || role == 5 )
                                  && wm.ball().pos().x < -20.0
                                  && self_min < opp_min + pressing
                                  && self_min < 20 );

    if ( ! wm.kickableTeammate()
         && ( self_min <= 3
              || defender_deep_press
              || ( self_min <= mate_min
                   && self_min < opp_min + pressing ) // pressing
              )
         )
    {
        dlog.addText( Logger::TEAM,
                      __FILE__": intercept" );
        Body_Intercept().execute( agent );
        agent->setNeckAction( new Neck_OffensiveInterceptNeck() );

        return true;
    }



// ── Offside Trap (improved) ──
    // Find our defensive line (2nd-deepest teammate x)
    double first = 0.0, second = 0.0;
    const auto t3_end = wm.teammatesFromSelf().end();
        for ( auto it = wm.teammatesFromSelf().begin();
              it != t3_end;
              ++it )
        {
            double x = (*it)->pos().x;
            if ( x < second )
            {
                second = x;
                if ( second < first )
                {
                    std::swap( first, second );
                }
            }
        }

   // Original offside trap: sprint forward when caught too deep
   double buf1 = 3.5;
   double buf2 = 4.5;

   if( me.x < -37.0 && opp_min < mate_min &&
       (homePos.x > -37.5 || wm.ball().inertiaPoint(opp_min).x > -36.0 ) &&
         second + buf1 > me.x && wm.ball().pos().x > me.x + buf2)
   {
        Body_GoToPoint( rcsc::Vector2D( me.x + 15.0, me.y ),
                        0.5, ServerParam::i().maxDashPower(),
                        ServerParam::i().maxDashPower(),
                        2, true, 5.0 ).execute( agent );

        if (wm.kickableOpponent() && wm.ball().distFromSelf() < 12.0)
            agent->setNeckAction(new Neck_TurnToBall());
        else
            agent->setNeckAction(new Neck_TurnToBallOrScan(4));
        return true;
   }

   // Aggressive line push: when WE have the ball or ball is in opp half,
   // defenders (roles 2-5) push up to compress the field and catch
   // opponents offside. Target = defense line + small buffer ahead.
   if ( ( role >= 2 && role <= 5 )
        && ( wm.kickableTeammate() || wm.ball().pos().x > 0.0 )
        && wm.ball().pos().x > -25.0
        && me.x < wm.ourDefenseLineX() - 2.0 )
   {
       // Push up to the defensive line (don't lag behind)
       double push_x = std::min( wm.ourDefenseLineX() + 1.0, -5.0 );
       // Don't push past half line
       push_x = std::min( push_x, 0.0 );

       if ( push_x > me.x + 1.5 )
       {
           Vector2D push_target( push_x, homePos.y );
           Body_GoToPoint( push_target, 0.5,
                           ServerParam::i().maxDashPower(),
                           ServerParam::i().maxDashPower(),
                           3, true, 5.0 ).execute( agent );
           agent->setNeckAction( new Neck_TurnToBallOrScan( 2 ) );

           dlog.addText( Logger::TEAM,
                         __FILE__": offside push → x=%.1f (defLine=%.1f)",
                         push_x, wm.ourDefenseLineX() );
           return true;
       }
   }

    if (std::min(self_min, mate_min) < opp_min){
        if (Bhv_Unmark().execute(agent))
            return true;
    }
    const double dash_power = Strategy::get_normal_dash_power( wm );

    double dist_thr = wm.ball().distFromSelf() * 0.1;
    if ( dist_thr < 1.0 ) dist_thr = 1.0;

    // ── Forward Run: offensive halves support attack ──────────────────────────
    // When a teammate has the ball in the opponent half and we are an offensive
    // half (role 7 or 8), sprint into the box to create a 3rd/4th attacker option.
    // This breaks the "only 2 attackers" pattern — now 3-4 players arrive.
    // The run target is diagonally into the box ahead of the ball.
    // Forward run: fires when we have/are winning the ball in opponent half.
    // Condition: kickableTeammate OR we're winning the ball race (our team reaches it
    // within 3 steps of the opponent). This keeps P7/P8 advanced even between passes.
    bool our_team_has_initiative = wm.kickableTeammate()
                                   || std::min( self_min, mate_min ) <= opp_min + 3;
    if ( our_team_has_initiative
         && ( role == 7 || role == 8 )
         && wm.ball().pos().x > 5.0
         && me.x < wm.offsideLineX() - 1.0 )
    {
        double run_x = std::min( wm.ball().pos().x + 12.0, wm.offsideLineX() - 1.5 );
        run_x = std::max( run_x, 20.0 );  // mantener posición avanzada mínima
        double run_y = ( role == 7 ) ? -8.0 : 8.0;
        run_y = std::max( -28.0, std::min( 28.0, run_y ) );

        Vector2D run_target( run_x, run_y );

        if ( run_target.x > me.x + 2.0 )
        {
            target_point = run_target;
            dlog.addText( Logger::TEAM,
                          __FILE__": forward run → (%.1f, %.1f)",
                          run_target.x, run_target.y );
            agent->debugClient().addMessage( "FwdRun" );
        }
    }

    // ── 3-Ring Unmarking (Cyrus 2022 TDP) ──
    // When a teammate has the ball and we are an offensive player,
    // search 24 candidate positions in 3 rings (2m/4m/8m) around
    // our current target. Pick the position where:
    //   1) We can reach before opponents (valid receive point)
    //   2) It's ahead of the ball (forward movement)
    //   3) It's not offside
    // Among valid positions, pick the one closest to an opponent
    // (to drag defenders and create space for others).
    if ( wm.kickableTeammate()
         && role >= 6
         && wm.ball().pos().x > -25.0
         && wm.self().pos().x < wm.offsideLineX() + 2.0 )
    {
        const double rings[] = { 2.0, 4.0, 8.0 };
        const int n_dirs = 8;
        Vector2D best_pos = target_point;
        double best_score = -1000.0;
        bool found = false;

        for ( int r = 0; r < 3; ++r )
        {
            for ( int d = 0; d < n_dirs; ++d )
            {
                double angle = 360.0 / n_dirs * d;
                double rad = angle * M_PI / 180.0;
                Vector2D candidate( target_point.x + rings[r] * std::cos( rad ),
                                    target_point.y + rings[r] * std::sin( rad ) );

                // Bounds check
                if ( candidate.x > 51.0 || candidate.x < -50.0 ) continue;
                if ( candidate.absY() > 33.0 ) continue;
                // Must not be offside
                if ( candidate.x > wm.offsideLineX() - 0.5 ) continue;
                // Must be forward relative to ball (through-pass territory)
                if ( candidate.x < wm.ball().pos().x - 3.0 ) continue;

                // How many cycles for me to reach this point
                double my_dist = wm.self().pos().dist( candidate );
                double my_cycles = my_dist / wm.self().playerType().realSpeedMax();

                // Check nearest opponent distance to candidate
                double min_opp_dist = 1000.0;
                for ( const PlayerObject * opp : wm.opponents() )
                {
                    if ( ! opp || opp->isGhost() ) continue;
                    double od = opp->pos().dist( candidate );
                    if ( od < min_opp_dist ) min_opp_dist = od;
                }
                double opp_cycles = min_opp_dist / 1.0;  // ~max opp speed

                // I must reach before opponent (with margin)
                if ( my_cycles > opp_cycles - 1.0 ) continue;

                // Score: prefer positions that are
                //   - forward (higher x)
                //   - close to an opponent (drag defenders, create space)
                //   - not too far from current target (stay in formation)
                double score = candidate.x * 0.5            // forward bonus
                             - min_opp_dist * 0.3           // closer to opp = better (drag)
                             - my_dist * 0.1;               // don't stray too far

                // CenterForward (P11) must stay central — wide unmarking
                // abandons the penalty spot and creates only corner shots.
                if ( role == 11 && candidate.absY() > 8.0 )
                    score -= ( candidate.absY() - 8.0 ) * 1.2;

                if ( score > best_score )
                {
                    best_score = score;
                    best_pos = candidate;
                    found = true;
                }
            }
        }

        if ( found && best_pos.dist( target_point ) > 1.0 )
        {
            target_point = best_pos;
            dlog.addText( Logger::TEAM,
                          __FILE__": 3-ring unmark → (%.1f, %.1f)",
                          target_point.x, target_point.y );
        }
    }

    // ── Coordinated Play-On Marking (Hungarian-style deterministic) ──────────
    // Same principle as set-play marking: sort threats by danger, sort
    // defenders by home-y, assign by rank. Every defender independently
    // computes the same assignment → no double-marking without communication.
    // Cyrus pass-cut formula positions marker on the opp→ball line.
    if ( role >= 2 && role <= 5
         && wm.ball().pos().x < 5.0
         && ! wm.kickableTeammate() )
    {
        const Vector2D our_goal( -ServerParam::i().pitchHalfLength(), 0.0 );
        const Vector2D home = Strategy::i().getPosition( wm.self().unum() );
        const Vector2D ball_predict = ( opp_min < 50
                                        ? wm.ball().inertiaPoint( opp_min )
                                        : wm.ball().pos() );

        // --- Build threat list (all eligible opponents) ---
        std::vector<const PlayerObject *> threats;
        for ( const PlayerObject * opp : wm.opponents() )
        {
            if ( ! opp || opp->isGhost() || opp->goalie() ) continue;
            if ( opp->pos().x > 5.0 ) continue;
            if ( opp->distFromBall() < 2.0 ) continue; // ball holder handled elsewhere
        	threats.push_back( opp );
        }
        // Sort threats: most dangerous first = closest to our goal
        std::sort( threats.begin(), threats.end(),
                   [&our_goal]( const PlayerObject * a, const PlayerObject * b ) {
                       return a->pos().dist( our_goal ) < b->pos().dist( our_goal );
                   } );

        // --- Build marker list (all defenders roles 2-5 on our team) ---
        // Sorted by home-position y (ascending) for lateral role matching
        std::vector<std::pair<double, int>> marker_hy_unum; // (home_y, unum)
        for ( int u = 2; u <= 11; ++u )
        {
            int r = Strategy::i().roleNumber( u );
            if ( r < 2 || r > 5 ) continue;
            const AbstractPlayerObject * tm = wm.ourPlayer( u );
            if ( ! tm ) continue;
            double hy = Strategy::i().getPosition( u ).y;
            marker_hy_unum.emplace_back( hy, u );
        }
        std::sort( marker_hy_unum.begin(), marker_hy_unum.end() );

        // --- Find my rank among markers ---
        int my_rank = -1;
        for ( int k = 0; k < (int)marker_hy_unum.size(); ++k )
        {
            if ( marker_hy_unum[k].second == wm.self().unum() )
            {
                my_rank = k;
                break;
            }
        }

        // --- Assign threat at my rank ---
        const PlayerObject * assigned = nullptr;
        if ( my_rank >= 0 && my_rank < (int)threats.size() )
            assigned = threats[my_rank];

        if ( assigned )
        {
            Vector2D opp_pos = assigned->pos() + assigned->vel();

            // Cyrus pass-cut: stand on the line opp→ball, 1-5m from opp
            double opp_ball_dist = opp_pos.dist( ball_predict );
            double dist_eval = std::min( 5.0, std::max( 1.0, opp_ball_dist / 10.0 ) );

            Vector2D pass_cut_dir = ball_predict - opp_pos;
            double dir_len = pass_cut_dir.r();
            if ( dir_len > 0.5 )
                pass_cut_dir *= ( dist_eval / dir_len );

            Vector2D mark_point = opp_pos + pass_cut_dir;

            // Blend 60% mark / 40% home — stay in formation
            Vector2D blended( mark_point.x * 0.6 + home.x * 0.4,
                              mark_point.y * 0.6 + home.y * 0.4 );

            // Clamp: max 6m from home
            Vector2D shift = blended - home;
            if ( shift.r() > 6.0 ) { shift.setLength( 6.0 ); blended = home + shift; }

            // Never advance past ball
            if ( blended.x > ball_predict.x - 2.0 )
                blended.x = ball_predict.x - 2.0;

            target_point = blended;

            agent->setArmAction( new Arm_PointToPoint( opp_pos ) );
            dlog.addText( Logger::TEAM,
                          __FILE__": coordinated mark opp%d (rank%d) → (%.1f, %.1f)",
                          assigned->unum(), my_rank,
                          target_point.x, target_point.y );
        }
    }

    // ── Defensive Ball Interceptor (RoboCIn 2023 style) ──────────────────────
    // When the ball is moving toward our goal (loose ball with dangerous velocity,
    // or opponent has it in our half), project the ball's trajectory and route
    // the nearest defender to the intercept point on that trajectory.
    // This beats static pass-cut marking when the ball is actively in motion.
    if ( role >= 2 && role <= 5
         && wm.ball().pos().x < 0.0
         && ! wm.kickableTeammate() )
    {
        Vector2D ball_dir;
        bool threat = false;

        if ( wm.kickableOpponent() && wm.ball().pos().x < -5.0 )
        {
            // Opponent has ball in our half → threat direction: toward our goal
            const Vector2D our_goal( -ServerParam::i().pitchHalfLength(), 0.0 );
            ball_dir = our_goal - wm.ball().pos();
            double len = ball_dir.r();
            if ( len > 0.1 ) { ball_dir *= ( 1.0 / len ); threat = true; }
        }
        else if ( wm.ball().vel().x < -0.5 && wm.ball().vel().r() > 0.5 )
        {
            // Loose ball moving toward our goal with meaningful speed
            ball_dir = wm.ball().vel();
            double len = ball_dir.r();
            if ( len > 0.1 ) { ball_dir *= ( 1.0 / len ); threat = true; }
        }

        if ( threat )
        {
            // Find intercept point on the first danger line the trajectory crosses.
            // Lines at x = -20 (midfield cover), -30 (deep cover), -36 (penalty area).
            const double lines[] = { -20.0, -30.0, -36.0 };
            bool found = false;
            Vector2D intercept_pt;

            for ( double ix : lines )
            {
                if ( ix >= wm.ball().pos().x ) continue;  // must be ahead of ball
                if ( std::fabs( ball_dir.x ) < 0.01 ) continue;

                double t = ( ix - wm.ball().pos().x ) / ball_dir.x;
                if ( t < 0.0 ) continue;

                double iy = wm.ball().pos().y + ball_dir.y * t;
                if ( std::fabs( iy ) > 30.0 ) continue;

                intercept_pt = Vector2D( ix, iy );
                found = true;
                break;
            }

            if ( found )
            {
                // Only override target if I am the nearest defender to intercept point
                double my_dist = me.dist( intercept_pt );
                bool i_am_nearest = true;

                for ( const PlayerObject * tm : wm.teammatesFromSelf() )
                {
                    if ( ! tm || tm->goalie() ) continue;
                    if ( tm->pos().dist( intercept_pt ) < my_dist - 1.5 )
                    {
                        i_am_nearest = false;
                        break;
                    }
                }

                if ( i_am_nearest && my_dist > 2.0 )
                {
                    target_point = intercept_pt;
                    dlog.addText( Logger::TEAM,
                                  __FILE__": ball-interceptor → (%.1f, %.1f) dist=%.1f",
                                  intercept_pt.x, intercept_pt.y, my_dist );
                }
            }
        }
    }

    // Offensive PointTo: signals "pass me here" to the ball holder.
    // Only forwards (role >= 8) who are:
    //   - AHEAD of the ball (never point backwards)
    //   - Behind offside line (legal position)
    //   - Closest to offside line (best through-ball target)
    // Always points FORWARD into space, never sideways or back.
    if ( role >= 8
         && wm.kickableTeammate()
         && wm.ball().distFromSelf() < 30.0
         && wm.self().pos().x > wm.ball().pos().x + 2.0
         && wm.self().pos().x < wm.offsideLineX() - 0.5 )
    {
        // Am I the most advanced non-offside teammate? (best receiver)
        bool i_am_best_receiver = true;
        for ( const PlayerObject * tm : wm.teammatesFromSelf() )
        {
            if ( ! tm || tm->goalie() ) continue;
            if ( tm->pos().x > wm.self().pos().x
                 && tm->pos().x < wm.offsideLineX()
                 && tm->distFromBall() < 30.0 )
            {
                i_am_best_receiver = false;
                break;
            }
        }

        if ( i_am_best_receiver )
        {
            // Point FORWARD into space: 5-8m ahead, pull Y toward center
            double through_x = std::min( wm.self().pos().x + 6.0,
                                         wm.offsideLineX() - 0.5 );
            // Pull Y toward goal center — creates diagonal runs
            double through_y = wm.self().pos().y * 0.5;

            // Ensure we're pointing forward relative to ball, never behind
            if ( through_x <= wm.ball().pos().x + 3.0 )
            {
                through_x = wm.ball().pos().x + 5.0;
            }

            Vector2D through_point( through_x, through_y );

            // Visual signal: PointTo (teammates see the arm direction)
            agent->setArmAction( new Arm_PointToPoint( through_point ) );

            // "R" system: send PassRequestMessage with target position.
            // The pass generator (strict_check_pass_generator) already
            // listens for these and gives bonus to through-passes that
            // match the requested direction. This is 4 chars of the
            // 10-char say budget — combines with other messages.
            agent->addSayMessage( new PassRequestMessage( through_point ) );

            dlog.addText( Logger::TEAM,
                          __FILE__": R-system through-pass request at (%.1f, %.1f)",
                          through_x, through_y );
        }
    }

    // ── YuShan 2025: Midfielder defensive retreat ──────────────────────────
    // When opponent is winning the ball race into our half, midfielders (6-8)
    // must drop back to form a compact defensive line in front of the penalty
    // area. YuShan identified this as a critical gap: midfielders stay in their
    // offensive positions while the rival already enters the penalty area.
    // HELIOS defends with a single compact line — we replicate that here.
    if ( role >= 6 && role <= 8
         && ! wm.kickableTeammate()
         && opp_min < std::min( self_min, mate_min ) + 3
         && wm.ball().pos().x < -10.0 )
    {
        // Target x: sit 5-8m behind the ball, but never deeper than -38
        // (that's the penalty area line — defenders handle that zone).
        // Target y: stay in own lane (home position y), don't drift sideways.
        double retreat_x = std::max( wm.ball().pos().x - 6.0, -36.0 );

        // Clamp: don't go deeper than formation home if that's already back enough
        retreat_x = std::min( retreat_x, homePos.x );

        // Only override if we are ahead of the retreat line (need to drop back)
        if ( me.x > retreat_x + 1.5 )
        {
            target_point.x = retreat_x;
            // Keep our lateral lane — don't converge to center
            target_point.y = homePos.y;

            dlog.addText( Logger::TEAM,
                          __FILE__": MID retreat → x=%.1f (ball=%.1f opp_min=%d)",
                          retreat_x, wm.ball().pos().x, opp_min );
            agent->debugClient().addMessage( "MID_Retreat" );
        }
    }

    // ── GK Cover: si el portero sale y deja la portería expuesta ─────────────
    // Cuando el GK se aleja del centro de la portería (persigue balón lateral),
    // el defensor más cercano a la portería se coloca en la línea de gol para
    // tapar el hueco. Solo roles 2-3 (centrales), solo cuando el balón amenaza.
    if ( ( role == 2 || role == 3 || role == 4 || role == 5 )
         && wm.ball().pos().x < -20.0
         && ! wm.kickableTeammate() )
    {
        const ServerParam & SP2 = ServerParam::i();
        const Vector2D goal_center( -SP2.pitchHalfLength(), 0.0 );

        // Buscar portero propio
        const PlayerObject * our_gk = nullptr;
        for ( const PlayerObject * tm : wm.teammates() )
        {
            if ( tm && tm->goalie() ) { our_gk = tm; break; }
        }

        if ( our_gk )
        {
            // El portero está "fuera" si se alejó lateralmente del centro
            const bool gk_out = ( our_gk->pos().absY() > SP2.goalHalfWidth() + 1.5
                                   || our_gk->pos().dist( goal_center ) > 7.0 );

            if ( gk_out )
            {
                // ¿Soy yo el defensor más cercano a la portería?
                double my_dist_goal = me.dist( goal_center );
                bool i_am_nearest = true;
                for ( const PlayerObject * tm : wm.teammates() )
                {
                    if ( ! tm || tm->goalie() ) continue;
                    int tr = Strategy::i().roleNumber( tm->unum() );
                    if ( tr < 2 || tr > 5 ) continue;
                    if ( tm->pos().dist( goal_center ) < my_dist_goal - 1.0 )
                    {
                        i_am_nearest = false;
                        break;
                    }
                }

                if ( i_am_nearest )
                {
                    // Posicionarse en la línea de gol bisectando el ángulo de tiro
                    // desde donde está el balón
                    double cover_y = wm.ball().pos().y * 0.4;
                    cover_y = std::max( -SP2.goalHalfWidth() + 0.4,
                               std::min(  SP2.goalHalfWidth() - 0.4, cover_y ) );
                    Vector2D cover_pos( -SP2.pitchHalfLength() + 1.0, cover_y );

                    dlog.addText( Logger::TEAM,
                                  __FILE__": GK-cover → (%.1f, %.1f) gk_y=%.1f",
                                  cover_pos.x, cover_pos.y,
                                  our_gk->pos().y );
                    agent->debugClient().addMessage( "GK_Cover" );
                    agent->debugClient().setTarget( cover_pos );

                    Body_GoToPoint( cover_pos, 0.4,
                                    SP2.maxDashPower() ).execute( agent );
                    agent->setNeckAction( new Neck_TurnToBall() );
                    return true;
                }
            }
        }
    }

    dlog.addText( Logger::TEAM,
                  __FILE__": Bhv_BasicMove target=(%.1f %.1f) dist_thr=%.2f",
                  target_point.x, target_point.y,
                  dist_thr );

    agent->debugClient().addMessage( "BasicMove%.0f", dash_power );
    agent->debugClient().setTarget( target_point );
    agent->debugClient().addCircle( target_point, dist_thr );

    if ( ! Body_GoToPoint( target_point, dist_thr, dash_power,
                           -1.0, 100, false
                           ).execute( agent ) )
    {
        Body_TurnToBall().execute( agent );
    }

    if ( wm.kickableOpponent()
         && wm.ball().distFromSelf() < 18.0 )
    {
        agent->setNeckAction( new Neck_TurnToBall() );
    }
    else
    {
        agent->setNeckAction( new Neck_TurnToBallOrScan( 0 ) );
    }

    return true;
}
