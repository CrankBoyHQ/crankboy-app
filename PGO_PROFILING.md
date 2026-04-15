# PGO Profiling Guide for CrankBoy

## Overview

This document describes how to collect optimal profile data for Profile-Guided Optimization (PGO).

## Why Profile Multiple Games?

Profiling a single game optimizes for that specific workload, which can hurt performance for other games. A diverse mix ensures the compiler optimizes for common emulator paths across different workloads.

## Recommended Profile Mix

Profile **3-4 diverse games** for 60-90 seconds each:

| Game Type | Purpose | Examples |
|-----------|---------|----------|
| **Simple puzzle/platformer** | Core interpreter loop, minimal I/O | Tetris, Dr. Mario, Alleyway |
| **Scrolling action** | LCD rendering, sprite handling, interrupts | Super Mario Land, Mega Man |
| **Audio-heavy** | APU (audio processing unit) paths | Kirby's Dream Land (PCM audio), Link's Awakening |
| **MBC/Advanced features** | Cartridge banking, RTC | Pokemon (MBC3 + RTC), Wario Land II |

## Why These Games?

Game Boy emulators spend time in:
1. **CPU interpreter** (~60-70%) - covered by any game
2. **LCD rendering** (~20-30%) - scrolling games hit this hard
3. **Audio synthesis** (~5-15%) - only audio-heavy games exercise this
4. **MBC banking** (~1-5%) - large ROM games need this

Profiling diverse games ensures the compiler sees all hot paths.

## Profile Session Structure

```bash
# Game 1: Simple game (2 min) - pure interpreter loop
# Game 2: Scrolling game (2 min) - graphics-heavy
# Game 3: Advanced features (2 min) - MBC, audio, RTC
# Total: ~6 minutes = solid profile data
```

## What to Avoid

- **Don't profile just menus** - not representative of gameplay
- **Don't profile the same game type 3x** - wastes profile budget
- **Don't profile for <30s per game** - not enough branch samples
- **Don't profile just one demanding game** - others may suffer

## Expected Results

A diverse profile mix typically yields:
- 5-10% overall FPS improvement
- Better frame pacing across all games
- More consistent performance (no regressions in unprofiled games)

## Workflow

```bash
# Step 1: Build instrumented simulator
make pgo-generate

# Step 2: Run simulator and play diverse game mix
# - Play each game type for 60-90 seconds
# - Focus on actual gameplay, not menus

# Step 3: Build optimized device binary
make pgo-device
```

## Troubleshooting

### Profile version mismatch
If you see errors like `profile count data file is version 'B52*', expected version 'A92r'`:
- Your simulator GCC and device GCC versions don't match
- Use the same GCC version for both, or skip PGO

### Missing profile warnings
If you see `profile count data file not found` warnings:
- This is expected for code paths not exercised during profiling
- The compiler will use heuristics for those paths

### No performance improvement
- Ensure you ran gameplay long enough (at least 30s per game)
- Check that PGO profiles were actually generated in `./pgo-data/`
- Some games may already be well-optimized by `-O2`
