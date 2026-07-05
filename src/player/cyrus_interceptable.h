// -*-c++-*-
/*
 * cyrus_interceptable.h — port de Cyrus2D OpenSource (src/move_def), autor
 * original: Nader Zare (2017). Portado a RoboTech 2026-07-03 (FASE 1 del plan
 * de defensa): predicción PROPIA de dónde/cuándo el RIVAL atrapa el balón
 * (CyrusOppInterceptTable), más fina que interceptTable().opponentStep() +
 * inertiaPoint. Cero dependencias del sistema Setting de Cyrus.
 *
 * Uso (patrón de Cyrus bhv_block):
 *   std::vector<Vector2D> cache = CyrusPlayerIntercept::createBallCache(wm);
 *   CyrusPlayerIntercept predictor(wm, cache);
 *   auto pred = predictor.predict(*opp, *opp->playerTypePtr(), 1000);
 *   CyrusOppInterceptTable best = CyrusPlayerIntercept::getBestIntercept(wm, pred);
 *   // best.cycle (1000 = inválido), best.current_position (trap point)
 *   // fallback recomendado: cycle > 100 || !current_position.isValid()
 */

#ifndef SRC_CYRUS_INTERCEPTABLE_H_
#define SRC_CYRUS_INTERCEPTABLE_H_

#include <rcsc/geom/vector_2d.h>
#include <rcsc/player/player_agent.h>
#include <vector>

class CyrusOppInterceptTable {
public:
    int cycle;
    rcsc::Vector2D current_position;
    int turn_cycle;
    double dist_ball;
    CyrusOppInterceptTable( int cycle, rcsc::Vector2D current_position,
                            int turn_cycle, double dist_ball );
};

class CyrusPlayerIntercept {
private:
    const rcsc::WorldModel & M_world;
    const std::vector<rcsc::Vector2D> & M_ball_pos_cache;

    CyrusPlayerIntercept();

public:
    CyrusPlayerIntercept( const rcsc::WorldModel & world,
                          const std::vector<rcsc::Vector2D> & ball_pos_cache )
        : M_world( world ),
          M_ball_pos_cache( ball_pos_cache )
    { }

    ~CyrusPlayerIntercept() { }

    std::vector<CyrusOppInterceptTable> predict( const rcsc::PlayerObject & player,
                                                 const rcsc::PlayerType & player_type,
                                                 const int max_cycle ) const;

    static CyrusOppInterceptTable getBestIntercept( const rcsc::WorldModel & wm,
                                                    const std::vector<CyrusOppInterceptTable> & table );

    static std::vector<rcsc::Vector2D> createBallCache( const rcsc::WorldModel & wm );

    //! conveniencia RoboTech: trap point del rival más rápido con fallback al
    //! interceptTable estándar. Devuelve false si no hay rival válido.
    static bool opponentTrap( const rcsc::WorldModel & wm,
                              rcsc::Vector2D * trap_pos,
                              int * trap_cycle );

private:
    bool canReachAfterTurnDash( const int cycle, const rcsc::PlayerObject & player,
                                const rcsc::PlayerType & player_type,
                                const double & control_area,
                                const rcsc::Vector2D & ball_pos ) const;

    int predictTurnCycle( const int cycle, const rcsc::PlayerObject & player,
                          const rcsc::PlayerType & player_type,
                          const double & control_area,
                          const rcsc::Vector2D & ball_pos ) const;

    bool canReachAfterDash( const int n_turn, const int n_dash,
                            const rcsc::PlayerObject & player,
                            const rcsc::PlayerType & player_type,
                            const double & control_area,
                            const rcsc::Vector2D & ball_pos ) const;
};

#endif /* SRC_CYRUS_INTERCEPTABLE_H_ */
