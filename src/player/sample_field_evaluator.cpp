// -*-c++-*-

/*
 *Copyright:

 Cyrus2D
 Modified by Omid Amini, Nader Zare
 
 Gliders2d
 Modified by Mikhail Prokopenko, Peter Wang

 Copyright (C) Hiroki SHIMORA

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "sample_field_evaluator.h"

#include "field_analyzer.h"
#include "simple_pass_checker.h"
#include "planner/action_state_pair.h"
#include "planner/cooperative_action.h"

#include <rcsc/player/player_evaluator.h>
#include <rcsc/common/server_param.h>
#include <rcsc/common/logger.h>
#include <rcsc/math_util.h>

#include <rcsc/player/world_model.h>

#include <rcsc/geom/voronoi_diagram.h>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <fstream>

// Optional: DNN-based field evaluation (trained from match logs)
// To enable: place field_eval_weights.txt in the bin/ directory
// Train with: scripts/training_field_eval/train_from_matches.sh
#ifdef HAVE_CPPDNN
#include <CppDNN/DeepNueralNetwork.h>
#endif

// #define DEBUG_PRINT

using namespace rcsc;

static const int VALID_PLAYER_THRESHOLD = 8;

// DNN field evaluator (loaded once, used as bonus on top of heuristic)
namespace {
#ifdef HAVE_CPPDNN
    static DeepNueralNetwork * s_field_eval_dnn = nullptr;
    static bool s_dnn_load_attempted = false;
#endif
    static bool s_dnn_available = false;
}

static void try_load_field_eval_dnn()
{
#ifdef HAVE_CPPDNN
    if ( s_dnn_load_attempted ) return;
    s_dnn_load_attempted = true;

    std::ifstream test("./field_eval_weights.txt");
    if ( ! test.good() )
    {
        std::cerr << "[FieldEvalDNN] No weights file found, using heuristic only" << std::endl;
        return;
    }
    test.close();

    s_field_eval_dnn = new DeepNueralNetwork();
    s_field_eval_dnn->ReadFromKeras("./field_eval_weights.txt");
    s_dnn_available = true;
    std::cerr << "[FieldEvalDNN] Loaded field_eval_weights.txt successfully" << std::endl;
#endif
}

static double dnn_evaluate( const PredictState & state, const WorldModel & wm )
{
#ifdef HAVE_CPPDNN
    if ( ! s_dnn_available ) return 0.0;

    // Build 48-dim feature vector matching extract_from_logs.py
    Eigen::MatrixXd input(48, 1);

    // Ball features [0-3]
    input(0, 0) = state.ball().pos().x / 52.5;
    input(1, 0) = state.ball().pos().y / 34.0;
    input(2, 0) = state.ball().vel().x / 3.0;
    input(3, 0) = state.ball().vel().y / 3.0;

    // Our players [4-25]: x, y for unum 1-11
    int idx = 4;
    for ( int u = 1; u <= 11; ++u )
    {
        const AbstractPlayerObject * p = state.ourPlayer(u);
        if ( p )
        {
            input(idx, 0)     = p->pos().x / 52.5;
            input(idx + 1, 0) = p->pos().y / 34.0;
        }
        else
        {
            input(idx, 0)     = 0.0;
            input(idx + 1, 0) = 0.0;
        }
        idx += 2;
    }

    // Opponent players [26-47]: x, y for unum 1-11
    for ( int u = 1; u <= 11; ++u )
    {
        const AbstractPlayerObject * p = state.theirPlayer(u);
        if ( p )
        {
            input(idx, 0)     = p->pos().x / 52.5;
            input(idx + 1, 0) = p->pos().y / 34.0;
        }
        else
        {
            input(idx, 0)     = 0.0;
            input(idx + 1, 0) = 0.0;
        }
        idx += 2;
    }

    // Forward pass
    s_field_eval_dnn->Calculate(input);
    double score = s_field_eval_dnn->mOutput(0, 0);  // sigmoid output [0, 1]

    // Scale to meaningful range: 0.5 neutral, >0.5 favorable.
    // ±30 compite con los bonuses de zona (~6-12) y el término Voronoi (≤40)
    // sin tapar el shoot bonus (~90k). Con ±2 el modelo era decorativo.
    return (score - 0.5) * 60.0;  // maps [0,1] → [-30, +30]
#else
    (void)state; (void)wm;
    return 0.0;
#endif
}


/*-------------------------------------------------------------------*/
/*!

 */
static double evaluate_state( const PredictState & state , const rcsc::WorldModel & wm );


/*-------------------------------------------------------------------*/
/*!

 */
SampleFieldEvaluator::SampleFieldEvaluator()
{
    try_load_field_eval_dnn();
}

/*-------------------------------------------------------------------*/
/*!

 */
SampleFieldEvaluator::~SampleFieldEvaluator()
{

}

/*-------------------------------------------------------------------*/
/*!

 */
double
SampleFieldEvaluator::operator()(const PredictState &state,
                                 const std::vector<ActionStatePair> &path,
                                 const rcsc::WorldModel &wm) const
{
    const double final_state_evaluation = evaluate_state( state , wm);

    double result = final_state_evaluation;

    // DNN bonus: trained from match logs (adds [-2, +2] to score)
    result += dnn_evaluate( state, wm );

    // ── RoboCIn Localization: zone-aware action bonuses ──────────────────────
    // Field divided into 3 zones by x:
    //   Defensive  (Z1): x < -20  → escaping pressure is top priority
    //   Midfield   (Z2): -20..15  → spacing and forward progression valued
    //   Offensive  (Z3): x > 15   → goal proximity and shot creation valued
    //
    // Rules derived from RoboCIn 2023/2024 TDP §3 (zone classifier):
    //   - Backward passes get no bonus (ever) — they waste sequences
    //   - Forward passes get zone-scaled bonus: bigger in midfield/offensive
    //   - Dribble forward in sparse zone (Z2/Z3) is rewarded
    //   - Dribble backward always penalized
    if ( ! path.empty() )
    {
        for ( size_t i = 0; i < path.size(); ++i )
        {
            const auto & asp = path[i];

            // Ball origin for this action
            const Vector2D & origin = ( i == 0 )
                ? wm.ball().pos()
                : path[i-1].state().ball().pos();
            const Vector2D & dest = asp.action().targetPoint();

            double dx = dest.x - origin.x;
            double dy = dest.y - origin.y;
            double forward_gain = dx;  // positive = toward opp goal

            // Zone of the origin
            double zone_mult;
            if ( origin.x < -20.0 )
                zone_mult = 1.0;   // Z1: modest bonus — any forward progress helps
            else if ( origin.x < 15.0 )
                zone_mult = 2.0;   // Z2: midfield — forward progression is crucial
            else
                zone_mult = 3.0;   // Z3: offensive — maximize goal threat

            if ( asp.action().category() == CooperativeAction::Pass )
            {
                // #3 Cutback (pase atrás desde el fondo/banda hacia el área central):
                // el balón llega al punto de penalti desde una posición honda y
                // abierta. Es de altísimo valor (remate franco) pero con la regla
                // vieja caía como "pase hacia atrás" (dx<=0) y se PENALIZABA — una
                // causa directa del túnel lateral. Lo premiamos fuerte y salteamos
                // el resto de la lógica direccional. Umbrales: destino en el área
                // central (x>30, |y|<12) y origen hondo (x>36) o más abierto que
                // el destino (banda → centro).
                const bool is_cutback =
                    dest.x > 30.0 && std::fabs( dest.y ) < 12.0
                    && ( origin.x > 36.0
                         || std::fabs( origin.y ) > std::fabs( dest.y ) + 8.0 );

                if ( is_cutback )
                {
                    // O3 (2026-07-05): 6→12. Con O1/O2 el receptor del cutback
                    // ya EXISTE (P7 en el borde, P11 en el penal); +6 no
                    // competía con el término de hueco Voronoi (hasta +40).
                    // P2a PROBADO Y REVERTIDO (2026-07-12): 12→18 no movió
                    // los toques centrales (5%) y la tanda salió con GF 0.25.
                    // LECCIÓN: perillas finas del evaluador son indetectables
                    // con tandas secuenciales de N=20 (GF varía 0.25-0.71
                    // entre tandas iguales) — si se retoman, usar el harness
                    // A/B pareado (run_ab_vs_opponent.sh).
                    result += 12.0;
                }
                else if ( forward_gain > 0.0 )
                {
                    // Forward pass: bonus scales with advancement × zone
                    double angle = std::fabs( std::atan2( dy, dx ) * 180.0 / M_PI );
                    double direction_mult = ( angle <= 30.0 ) ? 1.5   // through
                                         : ( angle <= 70.0 ) ? 1.0   // diagonal
                                         :                     0.4;  // wide cross

                    result += zone_mult * direction_mult
                              * std::min( forward_gain / 10.0, 2.0 );
                }
                else if ( forward_gain < -3.0 )
                {
                    // Retroceso real: malus pequeño escalado por zona.
                    // Lateral (|dx| <= 3) sigue sin bonus ni malus.
                    result -= std::min( -forward_gain / 10.0, 1.5 )
                              * zone_mult * 0.5;
                }
            }
            else if ( asp.action().category() == CooperativeAction::Dribble )
            {
                if ( forward_gain > 0.5 )
                    result += zone_mult * 0.4;   // dribble forward: small reward
                else
                    result -= 0.5;               // dribble backward: penalize
            }
        }
    }

    return result;
}


/*-------------------------------------------------------------------*/
/*!

 */
static
double
evaluate_state( const PredictState & state, const rcsc::WorldModel & wm )
{
    const ServerParam & SP = ServerParam::i();

    const AbstractPlayerObject * holder = state.ballHolder();

#ifdef DEBUG_PRINT
    dlog.addText( Logger::ACTION_CHAIN,
                  "========= (evaluate_state) ==========" );
#endif

    //
    // if holder is invalid, return bad evaluation
    //
    if ( ! holder )
    {
#ifdef DEBUG_PRINT
        dlog.addText( Logger::ACTION_CHAIN,
                      "(eval) XXX null holder" );
#endif
        return - DBL_MAX / 2.0;
    }

    // RoboCIn 2024 §2: Strongly penalize any action chain that ends with
    // the opponent gaining possession. Zone-scaled: losing the ball at x=40
    // is far less dangerous than at x=-30, and a flat penalty made risky
    // vertical passes always lose against safe lateral recycling.
    //
    // O6 (2026-07-05): el escalado x*4 era simbólico (perder en x=45 seguía
    // costando −320 contra un cutback de +12 y un hueco de +40) — razón
    // matemática de que el portador SIEMPRE reciclara a banda aunque el
    // receptor central existiera (track O1-O5: ocupación y carreras ✓ pero
    // conversión plana). Alivio adicional SOLO en zona de definición (x>25):
    // perder el balón junto a SU área es la pérdida barata (deben cruzar
    // toda la cancha; tenemos rest-defense P6 + trigger estrecho). En campo
    // propio sigue siendo catastrófico (−500..−420, sin cambio).
    //   x:    0     20     30     36     40     45    ≥47
    //   pen: −500  −420   −320   −224   −160   −80   −60(cap)
    if ( holder->side() != state.ourSide() )
    {
#ifdef DEBUG_PRINT
        dlog.addText( Logger::ACTION_CHAIN,
                      "(eval) XXX opponent gains possession" );
#endif
        const double bx = state.ball().pos().x;
        double loss = -500.0 + std::max( 0.0, bx ) * 4.0
                             + std::max( 0.0, bx - 25.0 ) * 12.0;
        return std::min( loss, -60.0 );
    }

    const int holder_unum = holder->unum();


    //
    // ball is in opponent goal
    //
    if ( state.ball().pos().x > + ( SP.pitchHalfLength() - 0.1 )
         && state.ball().pos().absY() < SP.goalHalfWidth() + 2.0 )
    {
#ifdef DEBUG_PRINT
        dlog.addText( Logger::ACTION_CHAIN,
                      "(eval) *** in opponent goal" );
#endif
        return +1.0e+7;
    }

    //
    // ball is in our goal
    //
    if ( state.ball().pos().x < - ( SP.pitchHalfLength() - 0.1 )
         && state.ball().pos().absY() < SP.goalHalfWidth() )
    {
#ifdef DEBUG_PRINT
        dlog.addText( Logger::ACTION_CHAIN,
                      "(eval) XXX in our goal" );
#endif

        return -1.0e+7;
    }


    //
    // out of pitch
    //
    if ( state.ball().pos().absX() > SP.pitchHalfLength()
         || state.ball().pos().absY() > SP.pitchHalfWidth() )
    {
#ifdef DEBUG_PRINT
        dlog.addText( Logger::ACTION_CHAIN,
                      "(eval) XXX out of pitch" );
#endif

        return - DBL_MAX / 2.0;
    }


    //
    // set basic evaluation
    //

    // G2d: to retrieve opp team name 
    // C2D: Helios 18 Tune removed -> replace with BNN
    // bool heliosbase = false;
    // if (wm.opponentTeamName().find("HELIOS_base") != std::string::npos)
    //     heliosbase = true;

    // G2d: number of direct opponents
        int opp_forward = 0;

        Vector2D egl (52.5, -8.0);
        Vector2D egr (52.5, 8.0);
        Vector2D left = egl - wm.self().pos();
        Vector2D right = egr - wm.self().pos();

        Sector2D sector(wm.self().pos(), 0.0, 10000.0, left.th(), right.th());

        for (auto of = wm.opponentsFromSelf().begin();
             of != wm.opponentsFromSelf().end();
             ++of)
        {
                if ( sector.contains( (*of)->pos() ) && !((*of)->goalie()) )
                opp_forward++;
        }

        double weight = 1.0;
        if (wm.ball().pos().x > 35.0)
            weight = 0.3;

	double depth = 10.0;
    // C2D: Helios 18 Tune removed -> replace with BNN
	// if (heliosbase)
	// 	depth = 0.0;

    double point = state.ball().pos().x * weight;

    // Non-linear bonus for proximity to opponent goal (Z3 — offensive zone),
    // ESCALADO POR CENTRALIDAD. Antes dependía solo de X → un balón en (50,-27)
    // (esquina muerta) valía IGUAL que en (50,0) (punto de penalti), así que el
    // ataque derivaba a las bandas/esquina (P9/P7 camponeaban el córner, 0 toques
    // centrales en el área, 0 goles). Un balón hondo pero abierto es casi un
    // callejón sin salida: lo descontamos por |y|.
    if ( state.ball().pos().x > 15.0 )
    {
        double approach = state.ball().pos().x - 15.0;  // 0..37.5
        const double aby = state.ball().pos().absY();
        const double central_factor = ( aby < 10.0 )
            ? 1.0
            : std::max( 0.25, 1.0 - ( aby - 10.0 ) / 30.0 );  // |y|=10→1.0, 25→0.5, 40→0.25
        point += approach * approach * 0.08 * central_factor;  // centro ~4× la esquina
    }

        Vector2D best_point = ServerParam::i().theirTeamGoalPos();

    // G2d: new eval function
    // C2D: replace PlayerPtrCont::const_iterator with auto
        if ( wm.ball().pos().x < depth || opp_forward == 0 )
	{
		// stay with best point = opp goal
	}
	else
	{

		if ( wm.ball().pos().x < 35.0 &&  state.ball().pos().x > -15.0 )
		{
                       VoronoiDiagram vd;

                        std::vector<Vector2D> vd_cont;
                        for ( auto o = wm.opponentsFromSelf().begin();
                                o != wm.opponentsFromSelf().end();
                                ++o )
                        {
                                   vd.addPoint((*o)->pos());
                        }

                        vd.compute();


			    double max_dist = -1000.0;

                            // En zona media exigir progresión: los huecos de Voronoi
                            // laterales/atrasados alimentaban la circulación sin profundidad.
                            double min_vertex_x = state.ball().pos().x - 5.0;
                            if ( state.ball().pos().x > -10.0
                                 && state.ball().pos().x < 25.0 )
                            {
                                min_vertex_x = state.ball().pos().x + 2.0;
                            }

                            for ( VoronoiDiagram::Vector2DCont::const_iterator p = vd.vertices().begin(),
                                      end = vd.vertices().end();
                                          p != end;
                                          ++p )
                            {
						if ( (*p).x < min_vertex_x || (*p).x > 52.5 || fabs((*p).y) > 34.0 )
							continue;

						if ( ( (*p) - state.ball().pos() ).length() > 34.0 )
							continue;

						double min_dist = 1000.0;
						double our_dist = 1000.0;

                                                for ( auto of = wm.opponentsFromSelf().begin();
                                                        of != wm.opponentsFromSelf().end();
                                                        ++of )
                                                {
                                                        Vector2D tmp = (*of)->pos() - (*p);
                                                        if ( min_dist > tmp.length() )
                                                                min_dist = tmp.length();
                                                }

                                                for ( auto of = wm.teammatesFromSelf().begin();
                                                        of != wm.teammatesFromSelf().end();
                                                        ++of )
                                                {
							if ((*of)->pos().x > wm.offsideLineX() + 1.0) continue;
                                                        Vector2D tmp = (*of)->pos() - (*p);
                                                        if ( our_dist > tmp.length() )
                                                                our_dist = tmp.length();
                                                }

						Vector2D tmp = wm.self().pos() - (*p); 
						if ( wm.self().pos().x < (*p).x && tmp.length() > 7.0 && our_dist > tmp.length() )
                                                        our_dist = tmp.length();

					// #4 Sesgo de centralidad en zona profunda: en x>30 un hueco de
					// Voronoi pegado a la banda alimenta el "túnel lateral" (centros
					// muertos). A igualdad de espacio, preferir el hueco más central
					// (half-space / punto de penalti) restando un término por |y|.
					// No afecta la zona media (x<=30), donde abrir a banda sí sirve.
					double vscore = min_dist - our_dist;
					if ( (*p).x > 30.0 )
						vscore -= std::max( 0.0, (*p).absY() - 8.0 ) * 0.5;

					if (max_dist < vscore )
					{
						max_dist = vscore;
						best_point = (*p);
					}

			    }

                        std::vector<Vector2D> OffsideSegm_cont;
                        std::vector<Vector2D> OffsideSegm_tmpcont;

                        Vector2D y1( wm.offsideLineX(), -34.0);
                        Vector2D y2( wm.offsideLineX(), 34.0);

                        Vector2D z1( wm.offsideLineX(), -34.0);
                        Vector2D z2( wm.offsideLineX(), 34.0);

                        if (wm.ball().pos().x > 25.0)
                        {
                                if (wm.ball().pos().y < 0.0)
                                        y2.y = 20.0;
                                if (wm.ball().pos().y > 0.0)
                                        y1.y = -20.0;
                        }
                        if (wm.ball().pos().x > 36.0)
                        {
                                if (wm.ball().pos().y < 0.0)
                                        y2.y = 8.0;
                                if (wm.ball().pos().y > 0.0)
                                        y1.y = -8.0;
                        }

                                z1.x = y1.x + 6.0;
				if (z1.x > 52.5)
					z1.x = 52.0;

                                z2.x = y2.x + 6.0;
				if (z2.x > 52.5)
					z2.x = 52.0;

                                z1.y = y1.y;
                                z2.y = y2.y;


                        Line2D offsideLine (y1, y2);
                        Line2D forwardLine (z1, z2);

                            for ( VoronoiDiagram::Segment2DCont::const_iterator p = vd.segments().begin(),
                                      end = vd.segments().end();
                                          p != end;
                                          ++p )
                            {
                                Vector2D si = (*p).intersection( offsideLine );
                                Vector2D fi = (*p).intersection( forwardLine );
                                if (si.isValid() && fabs(si.y) < 34.0 && fabs(si.x) < 52.5)
                                {
                                        OffsideSegm_tmpcont.push_back(si);
                                }
                                if (fi.isValid() && fabs(fi.y) < 34.0 && fabs(fi.x) < 52.5 && wm.ball().pos().x < 37.0)
                                {
                                        OffsideSegm_tmpcont.push_back(fi);
                                }
                            }

                            for ( std::vector<Vector2D>::iterator p = OffsideSegm_tmpcont.begin(),
                                      end = OffsideSegm_tmpcont.end();
                                          p != end;
                                          ++p )
                            {

						if ( (*p).x < state.ball().pos().x - 25.0 || (*p).x > 52.5 || fabs((*p).y) > 34.0 )
							continue;

						if ( ( (*p) - state.ball().pos() ).length() > 34.0 )
							continue;


						double min_dist = 1000.0;
						double our_dist = 1000.0;

                                                for ( auto of = wm.opponentsFromSelf().begin();
                                                        of != wm.opponentsFromSelf().end();
                                                        ++of )
                                                {
                                                        Vector2D tmp = (*of)->pos() - (*p);
                                                        if ( min_dist > tmp.length() )
                                                                min_dist = tmp.length();
                                                }

                                                for ( auto  of = wm.teammatesFromSelf().begin();
                                                        of != wm.teammatesFromSelf().end();
                                                        ++of )
                                                {
							if ((*of)->pos().x > wm.offsideLineX() + 1.0) continue;
                                                        Vector2D tmp = (*of)->pos() - (*p);
                                                        if ( our_dist > tmp.length() )
                                                                our_dist = tmp.length();
                                                }

						Vector2D tmp = wm.self().pos() - (*p);
						if ( wm.self().pos().x < (*p).x && tmp.length() > 7.0 && our_dist > tmp.length() )
                                                        our_dist = tmp.length();


					// O3 (2026-07-05): mismo sesgo de centralidad que el 1er
					// lazo (L515-517) — este 2º lazo (puntos sobre la línea de
					// offside) seguía premiando el hueco de la esquina.
					double vscore2 = min_dist - our_dist;
					if ( (*p).x > 30.0 )
						vscore2 -= std::max( 0.0, (*p).absY() - 8.0 ) * 0.5;

					if (max_dist < vscore2 )
					{
						max_dist = vscore2;
						best_point = (*p);
					}
			    }
		}
	}


    dlog.addText( Logger::TEAM, __FILE__": best point=(%.1f %.1f)", best_point.x, best_point.y);


    point += std::max( 0.0, 40.0 - best_point.dist( state.ball().pos() ) );

//  point += std::max( 0.0, 40.0 - ServerParam::i().theirTeamGoalPos().dist( state.ball().pos() ) );

#ifdef DEBUG_PRINT
    dlog.addText( Logger::ACTION_CHAIN,
                  "(eval) eval-center (%d) state ball pos (%f, %f)",
                  evalcenter, state.ball().pos().x, state.ball().pos().y );

    dlog.addText( Logger::ACTION_CHAIN,
                  "(eval) initial value (%f)", point );
#endif

    //
    // add bonus for goal, free situation near offside line
    //
    if ( FieldAnalyzer::can_shoot_from( holder->unum() == state.self().unum(),
                                        holder->pos(),
                                        state.getPlayers( new OpponentOrUnknownPlayerPredicate( state.ourSide() ) ),
                                        VALID_PLAYER_THRESHOLD ) )
    {
        // Proportional shoot bonus: larger angle = better shooting position.
        // Replaces flat 1e+6 — allows agent to distinguish tight angles from open ones.
        // Scale 1000 * degrees: max ~90° → 90,000, well above all other bonuses (~200 max).
        const Vector2D & hpos = holder->pos();
        const AngleDeg ang1 = ( Vector2D( SP.pitchHalfLength(),  SP.goalHalfWidth() ) - hpos ).th();
        const AngleDeg ang2 = ( Vector2D( SP.pitchHalfLength(), -SP.goalHalfWidth() ) - hpos ).th();
        double shoot_angle_deg = ( ang1 - ang2 ).abs();
        if ( shoot_angle_deg > 180.0 ) shoot_angle_deg = 360.0 - shoot_angle_deg;
        point += 1000.0 * shoot_angle_deg;
#ifdef DEBUG_PRINT
        dlog.addText( Logger::ACTION_CHAIN,
                      "(eval) bonus for goal angle=%.1f bonus=%.0f (%f)",
                      shoot_angle_deg, 1000.0 * shoot_angle_deg, point );
#endif

        if ( holder_unum == state.self().unum() )
        {
            point += 500.0 * shoot_angle_deg;  // self-bonus: proportional to angle
#ifdef DEBUG_PRINT
            dlog.addText( Logger::ACTION_CHAIN,
                          "(eval) bonus for goal self %.0f (%f)",
                          500.0 * shoot_angle_deg, point );
#endif
        }
    }

    return point;
}
