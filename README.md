# Southpaw – 2D Fighting Game Engine

A 2D fighting game I built from scratch in C++17 and raylib to learn how fighting games work under the hood. Moves run on frame data loaded from JSON, hits are checked with custom collision (boxes and swept strike arcs), inputs are buffered so combos feel responsive, and any match can be replayed exactly from inputs alone.


## Features

**Simulation core** (`src/core`, no raylib dependency)
- **12-state fighter state machine**: Idle, WalkForward, WalkBackward, Crouch, JumpSquat, Airborne, Landing, Attack, Blockstun, Hitstun, Knockdown, KO. Every transition is checked against an explicit table (`kAllowed` in `sim.cpp`), and the tests confirm there are zero illegal transitions across 1M+ random frames.
- **Data-driven moves**: all 14 moves and the fighter stats live in [`data/moves.json`](data/moves.json) (startup, active and recovery frames, damage, hitstun, knockback, guard height, hitbox shape, cancel routes). The file is **hot-reloaded while the game runs**, so balance changes don't need a recompile. Bad edits are rejected with a line number and the previous data stays active.
- **Custom collision and combat math**, all in integer fixed point:
  - AABB hitboxes and hurtboxes, mirrored by facing.
  - **Strike arcs**: annular-sector hitboxes swept across the active frames. Each frame covers only the slice the limb travels through, tested exactly against boxes with integer geometry.
  - **Facing checks**: front-only strikes, auto-facing, and guard direction taken from where the attacker actually is, so cross-ups must be blocked the other way.
  - **Knockback**: ground slide with friction, launches and juggles with a juggle limit, knockback that grows with combo length, and corner pushback onto the attacker.
  - High/low/mid guard, chip damage, hitstop, counter hits, damage scaling.
- **6-frame input buffer**: presses stay live for 6 frames in a fixed ring buffer, so a combo input pressed slightly late still comes out on the first actionable frame. Also handles quarter-circle motion inputs and edge-triggered presses.
- **Deterministic replays**: a match is fully described by its config, the move-data hash and both players' inputs. Every frame's state is hashed (FNV-1a, field by field so padding never leaks in) so playback can prove it rebuilt the exact same match.
- **Zero per-frame heap allocations**: all state is fixed-size plain data. A counting `operator new` verifies this in the tests and the benchmark.

**Game** (`src/game`)
- Procedural skeletal animation driven entirely by sim state, so replays animate identically.
- Versus CPU (4 difficulty levels), local 2-player (keyboard or gamepads), training mode, and a replay viewer with live per-frame hash verification.
- Training mode: input history, frame data, frame advantage readout, hitbox overlay, frame-by-frame stepping, buffer on/off toggle, dummy modes.
- Hit sparks, screen shake, K.O. slow motion, and sound effects synthesized at startup (no asset files).

## Results

All numbers come from `southpaw_tests` and `southpaw_bench` (Release build, Apple M2 Pro).

**Input buffer.** The test attempts three two-hit chains with the follow-up pressed 0–5 frames after the starter's 3-frame cancel window opens:

```
      follow-up lateness (frames) 0 1 2 3 4 5
    no buffer:
      jab > cross            Y Y Y . . .
      crouch jab > low kick  Y Y Y . . .
      short kick > cross     Y Y Y . . .
    6-frame buffer:
      jab > cross            Y Y Y Y Y Y
      crouch jab > low kick  Y Y Y Y Y Y
      short kick > cross     Y Y Y Y Y Y
    combo success: 50% without buffer -> 100% with 6-frame buffer
```

Without the buffer, a press after the cancel window closes is simply lost. With it, the press survives until recovery ends and comes out as a link, and the starter's hitstun is long enough for it to still combo.

**Determinism.** CPU and random-input matches over **1,049,802 frames** (140 matches, mixed buffer and training settings) were recorded, then rebuilt from inputs alone. Result: 0 per-frame hash mismatches, 12/12 states visited, 0 illegal transitions. A single flipped button on one frame is detected on exactly that frame.

**Throughput** (headless, 5M frames, 0 heap allocations in every loop):

| Loop | frames/sec |
|---|---|
| Replay from inputs (sim step only) | ~12,000,000 |
| Replay + per-frame state hash validation | ~2,500,000 |
| Full frame: 2 CPU brains + step + hash + record | ~2,300,000 |

## Building

Requires CMake 3.16+, a C++17 compiler and raylib 5.x. If raylib isn't installed, CMake downloads it.

```bash
brew install cmake raylib        # macOS (optional: raylib is fetched otherwise)
cmake -S . -B build
cmake --build build -j
./build/southpaw                 # play
./build/southpaw_tests           # test suite (also: cd build && ctest)
./build/southpaw_bench 5000000   # headless benchmark
```

Command-line options:

```
southpaw --replay replays/file.sprp   # open a saved replay in the viewer
southpaw --verify replays/file.sprp   # headless: re-simulate and check every frame hash
southpaw --data path/to/moves.json    # alternate move data
southpaw --capture dir                # self-playing demo that saves screenshots
```

To build only the engine, tests and benchmark without raylib: `cmake -S . -B build -DSOUTHPAW_BUILD_GAME=OFF`.

## Controls

| | Move | LP | HP | LK | HK |
|---|---|---|---|---|---|
| Player 1 | W A S D | U | I | J | K |
| Player 2 | Arrows | , / Num4 | . / Num5 | M / Num1 | / / Num2 |
| Gamepad | D-pad / stick | X | Y | A | B |

Hold back to block, down-back to block lows; overheads must be blocked standing. The special move (`haymaker`) is quarter-circle forward + HP. The in-game *Controls & Moves* screen lists every move, generated from `moves.json`.

Training keys: `F1` hitboxes, `F3` pause, `F4` step one frame, `F5` force reload, `F6` toggle input buffer, `F7` dummy mode, `F8` reset.
