// -*-c++-*-
// RivalTuning (2026-07-13, roadmap "ajuste por rival") — versión ligera de la
// idea Setting de Cyrus: 4 perillas tácticas ajustables POR RIVAL desde
// ./rival_tuning.conf (CWD, como los demás assets), sin recompilar.
//
// Sin archivo o sin entrada para el rival: los defaults SON los valores
// actuales del equipo — comportamiento idéntico bit a bit. Eso hace que cada
// tanda con una entrada nueva traiga su propio control: los rivales sin
// entrada deben salir en su banda de siempre.
//
// Formato del .conf (líneas "clave_rival perilla valor", # comenta):
//   HELIOS pressing 13
// La clave matchea por substring contra worldModel.opponentTeamName()
// (HELIOS matchea HELIOS2025, HELIOS_base, ...).

#ifndef RIVAL_TUNING_H
#define RIVAL_TUNING_H

#include <rcsc/player/world_model.h>

class RivalTuning {
public:
    static RivalTuning & i();

    // carga lazy: no hace nada hasta que el WM conoce el nombre del rival;
    // parsea el .conf UNA vez y aplica la primera entrada que matchee
    void update( const rcsc::WorldModel & wm );

    int    pressBase()  const { return M_press_base; }   // bhv_basic_move
    double deepPressX() const { return M_deep_press_x; } // bhv_basic_move
    double wingX()      const { return M_wing_x; }       // strategy wing-tactic
    double wingY()      const { return M_wing_y; }

private:
    RivalTuning() = default;

    bool   M_loaded = false;
    // defaults = valores vigentes del equipo (NO cambiar aquí sin tanda)
    int    M_press_base   = 25;
    double M_deep_press_x = -20.0;
    double M_wing_x       = 15.0;
    double M_wing_y       = 12.0;
};

#endif // RIVAL_TUNING_H
