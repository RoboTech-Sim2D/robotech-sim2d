# Parches requeridos en librcsc (IMPRESCINDIBLE para compilar)

El denoiser de localización (`src/player/localization_denoiser*.cpp`) necesita
**3 métodos inline** que NO existen en la librcsc estándar. En la máquina
principal ya están aplicados (en `~/rc/tools/include/` y en el árbol fuente
`~/rc/tools/librcsc-support-v18/`). **Si compilas en otra máquina, aplícalos
a tu librcsc antes de compilar el equipo** — son solo headers (no hay que
recompilar la .so).

El paquete de competencia NO se ve afectado: `build_bin.sh` embebe nuestra
librcsc.so ya construida.

## 1. `rcsc/player/player_agent.h` — después del método `world()`

```cpp
    /*!
      \brief RoboTech denoiser: acceso mutable al world model
      para el denoiser team-side (espejo del world_not_const del fork Cyrus).
    */
    WorldModel & world_not_const()
      {
          return M_worldmodel;
      }
```

## 2. `rcsc/player/world_model.h` — después de `teammatesFromSelf()`

```cpp
    //! RoboTech denoiser: listas mutables para corrección de posiciones
    PlayerObject::Cont & mutableTeammatesFromSelf() { return M_teammates_from_self; }
    //! RoboTech denoiser: listas mutables para corrección de posiciones
    PlayerObject::Cont & mutableOpponentsFromSelf() { return M_opponents_from_self; }
```

## 3. `rcsc/player/abstract_player_object.h` — después del getter `pos()`

```cpp
    //! RoboTech denoiser: sobrescribe la posición estimada
    void denoiserOverridePos( const Vector2D & corrected )
      {
          M_pos = corrected;
      }
```

## Historial de parches previos en nuestra librcsc

- `doBipedalDash` / `world_not_const`-era ya divergían de la stock (jun-2026).
  El código del EQUIPO evita `doBipedalDash` a propósito (usa `doDash(power,
  dir)` estándar) para minimizar el acoplamiento; el denoiser es la única
  pieza que exige estos 3 métodos nuevos.
