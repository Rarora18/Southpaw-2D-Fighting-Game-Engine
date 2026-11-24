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
