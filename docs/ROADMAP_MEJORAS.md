# Roadmap de mejoras — base competitiva RoboTech (jul-2026)

Escrito como handoff: cada mejora incluye qué es, la evidencia que la motiva,
cómo implementarla (archivos concretos), su gate de validación y el riesgo.

## La regla que sostiene todo (NO negociable)

**Un cambio por tanda.** Tanda = `scripts/log_collect/collect_logs.sh 10
SRBIAU2D RoboCIn HELIOS2025` (~40 min). Cada cambio define su gate de métricas
ANTES de correr; si no mejora o empeora → revert inmediato. Los dos intentos
de trasplantar sistemas completos de Cyrus (defensa entera, N=30: GF=0 en 30
partidos) fallaron; **las piezas autocontenidas** (portero, interceptable,
scape_voronoi, denoiser) funcionaron. Herramientas de medición en
`scripts/analysis/` (side-aware) y el vigía `build/bin/slow_actions.csv`
(picos >70 ms = jugadores que pierden acciones; ignorar ciclos <20 y
fronteras 3000/6000).

**Contexto clave**: el benchmark de visión perfecta
(`scripts/ab_test/fullstate_bench.sh`, PROHIBIDO en competencia) midió el
techo con la lógica actual: **2.8 GF/partido y 6W/6 vs SRBIAU**. Estamos en
~0.71. La brecha es en su mayoría calidad de percepción y finalización.

---

## 1. Denoiser variante `ByAction` — 1 línea, valor potencial alto

- **Qué**: ya está portada y COMPILA (`localization_denoiser_by_action.cpp`).
  Cambiar en `sample_player.cpp` (ctor): `new LocalizationDenoiserByArea()` →
  `new LocalizationDenoiserByAction()`.
- **Por qué**: ByArea acota dónde *puede* estar el rival (física); ByAction
  además pondera a dónde *iría* un rival racional → estimaciones más finas.
- **Gate**: GF combinado SRBIAU+RoboCIn ≥0.8 (baseline ByArea: 0.71) y GA no
  peor. Vigilar `slow_actions.csv` (ByAction computa más por hipótesis).
- **Riesgo**: bajo; revert de 1 línea. Si empata con ByArea, quedarse con
  ByArea (más simple).

## 2. Pesos P2 (propuesta original del equipo) — 2 números

- **Qué**: `sample_field_evaluator.cpp`: bonus de cutback `+12` → `+15..20`;
  malus de centralidad Voronoi (ambos lazos, `vscore`/`vscore2`) `0.5` → `1.0`.
- **Por qué**: estaba bloqueada porque no existía receptor central (O1/O2 lo
  crearon) y la percepción era mala (denoiser). El término de hueco (+40)
  sigue dominando al cutback (+12).
- **Cómo**: DOS tandas (primero cutback, luego malus) — el evaluador tiene
  historial de sorpresas (ver switch-of-play jun-2026 en memoria).
- **Gate**: toques profundos centrales (hoy 4-10%, medir con
  `central_support.py`) ≥15%; GF ≥0.8; GA no peor.

## 3. BALÓN PARADO — la veta sin explotar (posible mina de oro)

- **Qué**: córners y tiros libres. NUNCA se han trabajado: la ejecución es
  helios-base de fábrica (`src/player/setplay/`).
- **Paso 0 (análisis, sin riesgo)**: script sobre los .rcg (competencia en
  `~/Descargas/Logs Competencia` + tandas) que clasifique cada gol por origen:
  ¿vino dentro de ~15 ciclos tras `corner_kick_*` / `free_kick_*` /
  `kick_in_*`? → cuántos goles regalamos/ganamos por balón parado hoy.
- **Ofensivo**: jugada ensayada de córner (corto al borde del área para
  remate del llegador; el segundo palo ya tiene ocupante por O1). Las
  formaciones de setplay de Cyrus ya están; la EJECUCIÓN (quién, cuándo) está
  en `bhv_set_play*.cpp`.
- **Defensivo**: marcaje en córner rival (hoy genérico; revisar
  `bhv_their_goal_kick_move` / cobertura de faltas retocada en jun).
- **Gate**: goles de balón parado por tanda (del script del paso 0).

## 4. Histéresis de stamina por línea + break-offside P9 — baratas

- **Histéresis** (patrón Cyrus): los gates secos (`stamina > 45%` en forward
  run/3-ring/scape de `bhv_basic_move.cpp`) causan parpadeo en la frontera.
  Reemplazar por par entrar/salir con flag por jugador: entra en ahorro
  <40%, sale >55% (Cyrus: back 4000/6000, half 3000/5000).
- **Break-offside P9** (Cyrus `mIs9BrokeOffside`): carrera anticipada del
  delantero al espacio cuando el balón avanza, desde campo propio. Port
  pequeño en `bhv_basic_move.cpp` para roles 9/10.
- **Gate**: GF/remates; stamina media de medios en 2ª mitad (rcg la trae).

## 5. Diagnóstico quirúrgico del ruido restante

- **Qué**: el denoiser corrige JUGADORES; el ruido de BALÓN/velocidades sigue.
  ¿Cuánto cuesta? `start.sh --fullstate reference` mantiene DOS world models
  (el ruidoso que decide + el perfecto de referencia) sin cambiar conducta.
- **Cómo (v1)**: partidos locales con `reference` + script que compare por
  ciclo el error del WM ruidoso vs verdad (por objeto: balón, rival por
  distancia, velocidades) → "presupuesto de ruido" restante por objeto.
- **Decisión que habilita**: si el error de BALÓN domina → extender el
  denoiser al balón (pieza nueva); si ya es bajo → el siguiente cuello no es
  percepción.

## 6. Ajuste por rival (la ventaja real de Cyrus, versión ligera)

- **Qué**: NO portar su Setting completo (receta en `docs/` y memoria si se
  quisiera); versión propia: JSON con 4-6 knobs nuestros por rival conocido
  (radio del press, trigger de wing-tactic, caps de línea, prob de tackle
  zonal) + tabla nombre→archivo + fallback a valores actuales.
- **Por qué**: contra débiles conviene presionar alto; contra HELIOS-class,
  replegar antes. Hoy los parámetros son únicos para todos.
- **Gate**: por rival objetivo (puntos vs Titas/Oxsy; GA vs YuShan).

## 7. Reentrenar el field_eval DNN — el track largo (mayor techo)

- **Estado**: apagado en entrega (medido −9 netGD con N=30 — el modelo viejo
  se entrenó con el equipo de formaciones corruptas y con etiqueta solo-ataque).
- **Qué cambiar** (pipeline ya existe en `scripts/training_field_eval/`):
  1. Etiqueta SIMÉTRICA: diferencia de goles en horizonte 100-150 ciclos
     (+1 anotamos / −1 encajamos / 0), no binaria "gol en 300c".
  2. Dataset SOLO posterior al 3-jul-2026 (formaciones sanas) y generado con
     el denoiser activo (que el modelo aprenda sobre lo que el equipo VE).
     `collect_logs.sh` produce ~90 partidos/noche.
  3. `class_weight` (los goles son raros) y validación temporal.
- **Vara de entrada**: A/B con `run_ab_vs_opponent.sh` (N≥15/condición);
  solo se enciende si netGD > 0. El evaluador guía TODA la búsqueda
  best-first — un evaluador aprendido bueno es cómo los top ganan.

## 8. Subir el nivel del sparring

- Los rivales medios están dominados (récords vs SRBIAU/RoboCIn). Incluir
  YuShan2025/TitasdaRobotica/FRA-UNIted en tandas regulares (docker+sudo;
  FRA es tiempo real — usar poco) y recalibrar gates contra ese tier.
- HELIOS-class sigue siendo termómetro, no objetivo de tuning.

---

## Orden sugerido

`1 → 3(paso 0) → 2 → 4 → 5 → 3(ejecución) → 6 → 8 → 7` — el 7 corre en
paralelo desde que haya dataset suficiente. Cada número = su(s) tanda(s) con
gate. La memoria persistente de Claude (sesiones previas) contiene el
historial completo de datos y decisiones si se retoma con el asistente.
