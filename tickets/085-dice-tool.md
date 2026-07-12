# Ticket 085 — Dice Tool

**Status:** Draft  
**Priority:** Normal  
**Created:** 2026-06-12  
**Component:** Core / Tool System

## Summary

A simple RNG-based dice rolling tool. Agents can roll any combination of dice and receive results. Fundamental utility that surfaces in games, narrative, random selection, and probabilistic reasoning.

## Motivation

Dice rolls are a primitive operation that agents shouldn't need to approximate with LLM-generated "randomness." A deterministic RNG tool gives agents access to genuine randomness (or seeded pseudo-randomness) with a clean interface. Small scope, high utility.

## Interface

Exposed as Animus tool:

### `dice`
```
dice(notation: string, count: int = 1, seed: string|null = null) → DiceResult
```

**Parameters:**
- `notation` — Dice notation string: `d6`, `2d10`, `3d8+4`, `1d20-2` (supports NdS±M)
- `count` — Number of times to roll the full expression (default 1). `dice("2d6", 10)` → 10 sets of 2d6
- `seed` — Optional seed string for reproducible rolls. Hashed to produce deterministic sequence. Null = system RNG.

**Result:**
```json
{
  "notation": "2d6",
  "results": [
    { "rolls": [3, 5], "sum": 8, "modifier": 0, "total": 8 },
    { "rolls": [1, 6], "sum": 7, "modifier": 0, "total": 7 }
  ],
  "count": 2,
  "seed_used": "abc123"
}
```

## Design Decisions

### Raw Results Over Interpretation
The tool returns numbers. The agent decides what they mean. No "success/failure" logic, no critical hit detection — that's the agent's job. The tool is a random number generator, not a game engine.

### NdS±M Notation
Standard dice notation covers the vast majority of use cases. `NdS` = N dice of S sides each. Optional `+M` or `-M` modifier. Parsing is trivial.

### Seeded Rolls for Narrative Consistency
An optional seed string allows agents to produce reproducible rolls. Useful for narrative contexts where "the same scene should play out the same way" matters. The seed is hashed (not used directly) to prevent agents from gaming outcomes by crafting seeds.

If seed is provided, the RNG is seeded once and all `count` rolls draw from that sequence. Same seed + same notation + same count = same results, always.

### No Advantage/Disadvantage
Keep v1 minimal. The agent can roll twice and pick higher/lower. Adding game-specific mechanics to a primitive tool is scope creep.

## Implementation Notes

- Use a standard PRNG (e.g., Mersenne Twister or xoshiro256**). Seed from `std::random_device` when no seed provided.
- Notation parsing: regex `^(\d*)d(\d+)([+-]\d+)?$` — capture group 1 defaults to 1 if empty.
- Each die in a roll is independent (a d6 rolling [3,5] means two separate d6 results, not one).
- Results array length = `count`. Each entry's `rolls` array length = N from notation.

## Files Changed

### C++
- New: `src/kernel/tools/DiceTool.cpp` — tool implementation
- New: `include/animus_kernel/DiceTool.h` — header
- Modified: tool registration (wherever tools are registered with the tool system)

### Lua
- `Dice` tool exposed to Lua scripting environment

## Testing

1. `dice("d6")` → single result, one roll, value 1-6
2. `dice("2d10")` → single result, two rolls, each 1-10
3. `dice("1d20+5")` → single result, one roll 1-20, modifier +5
4. `dice("d6", 10)` → 10 results, each with one roll 1-6
5. `dice("d6", 3, seed="test")` → deterministic, same results on repeated call
6. `dice("d6", 3, seed="test")` again → identical results to #5
7. `dice("3d8-2")` → single result, three rolls 1-8, modifier -2
8. Invalid notation → clear error message

## Scope

- Single tool: `dice`
- Standard dice notation (NdS±M)
- Optional seeded rolls
- Raw numerical results

**Out of scope:** Advantage/disadvantage, exploding dice, percentile dice as special case, fate dice, custom die faces, roll statistics (agent can compute these from results).
