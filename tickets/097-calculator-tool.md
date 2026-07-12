# Ticket 097 — Calculator Tool

**Status:** Open  
**Priority:** Low  
**Created:** 2026-06-22  
**Depends on:** None  
**Component:** Tools

## Summary

A deterministic calculator tool that allows agents to evaluate arithmetic and mathematical expressions without relying on LLM token generation for math. This prevents floating-point errors, order-of-operations mistakes, and hallucinated results that occur when LLMs attempt computation through pattern matching.

## Motivation

LLMs are unreliable at arithmetic. They pattern-match their way to answers that look correct but can be off by orders of magnitude, misplace decimal points, or get operator precedence wrong. For any agent that handles financial data, scientific calculations, scheduling, or quantitative analysis, a deterministic calculator is essential.

A calculator tool:
1. **Eliminates math errors** — the expression is evaluated by a real parser, not a language model
2. **Enables complex calculations** — chained operations, variables, unit conversions
3. **Builds agent confidence** — the agent knows the result is exact, not approximate
4. **Composable with other tools** — use after `ontology:upsert` to verify numerical properties, or after `search` to aggregate data points

## Design

### Actions

```
action: "eval" — evaluate a mathematical expression
action: "convert" — unit conversion (length, weight, temperature, time, data)
action: "help" — list available functions and constants
```

### `eval` action

**Parameters:**
- `expression` (string, required) — mathematical expression to evaluate

**Supported operations:**
- Basic arithmetic: `+`, `-`, `*`, `/`, `%` (modulo)
- Exponentiation: `^` or `**`
- Parentheses: `(`, `)`
- Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Boolean: `and`, `or`, `not`

**Built-in functions:**
- `abs(x)`, `ceil(x)`, `floor(x)`, `round(x, digits)`
- `min(...)`, `max(...)`, `clamp(x, lo, hi)`
- `sqrt(x)`, `cbrt(x)`, `pow(x, y)`
- `log(x)`, `log10(x)`, `log2(x)`, `exp(x)`
- `sin(x)`, `cos(x)`, `tan(x)`, `asin(x)`, `acos(x)`, `atan(x)`, `atan2(y, x)`
- `gcd(a, b)`, `lcm(a, b)`

**Constants:**
- `pi`, `e`, `tau`

**Examples:**
```
{"action": "eval", "expression": "2 + 3 * 4"}           → 14
{"action": "eval", "expression": "sqrt(144) + 2^3"}     → 20
{"action": "eval", "expression": "round(pi, 4)"}        → 3.1416
{"action": "eval", "expression": "max(1, 5, 3) * 2"}    → 10
{"action": "eval", "expression": "log(exp(1))"}          → 1
```

### `convert` action

**Parameters:**
- `value` (number, required) — value to convert
- `from` (string, required) — source unit (e.g., "km", "lbs", "fahrenheit")
- `to` (string, required) — target unit (e.g., "miles", "kg", "celsius")

**Supported categories:**
- Length: m, km, cm, mm, mile, yard, foot, inch, nautical_mile
- Weight: kg, g, lb, oz, ton (metric), ton_us
- Temperature: celsius, fahrenheit, kelvin
- Time: second, minute, hour, day, week, month (30d), year (365d)
- Data: byte, kb, mb, gb, tb, kib, mib, gib, tib
- Speed: mps, kmh, mph, knot

**Examples:**
```
{"action": "convert", "value": 100, "from": "km", "to": "miles"}  → 62.137
{"action": "convert", "value": 98.6, "from": "fahrenheit", "to": "celsius"}  → 37
```

## Implementation

### Expression evaluation

Two options:

1. **Embedded expression parser** (recommended) — a small recursive-descent parser in C++ (~200 lines). No external dependencies. Full control over syntax and security. Prevents `system()` calls, file access, or code injection.

2. **Library** — use a lightweight library like TinyExpr or muParser. Faster to integrate but adds a dependency.

Recommend option 1 for security (agents evaluate untrusted expressions) and minimal footprint.

### Unit conversion

Static conversion factor table. Each category has a base unit; conversions go through the base. Temperature needs special handling (offset + scale, not just multiplication).

### Security

- **No `eval()`** — never use host language eval. The parser is purpose-built.
- **No system calls** — no `system()`, `popen()`, file I/O, or network access.
- **Expression length limit** — reject expressions longer than 500 characters.
- **Recursion depth limit** — cap parser recursion at 50 levels.
- **Timeout** — if evaluation takes more than 100ms, abort (prevents pathological inputs).

## Files to create

| File | Description |
|------|-------------|
| `include/animus_kernel/tools/CalculatorTool.h` | Tool header |
| `src/kernel/tools/CalculatorTool.cpp` | Tool implementation + expression parser + conversion tables |

## Testing

1. `eval("2 + 2")` → 4
2. `eval("3 * (4 + 5)")` → 27
3. `eval("sqrt(2)^2")` → 2.0 (within float precision)
4. `eval("sin(pi)")` → ~0.0
5. `eval("max(1, 2, 3) + min(4, 5, 6)")` → 7
6. `convert(100, "km", "miles")` → ~62.137
7. `convert(32, "fahrenheit", "celsius")` → 0
8. `eval("import os")` → error (security)
9. `eval("")` → error (empty expression)
