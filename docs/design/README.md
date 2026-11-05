# Design Documents

Direction and functional-intent documents for major Animus systems. These are
living documents: each tracks a system's intent, contracts, and open questions
from design through implementation, and tickets are cut from them.

| Document | System | Status |
|---|---|---|
| [Interjection & Device Events](interjection-and-events.md) | The shared kernel primitive: inbound events inserted into running prompt chains | Draft (v0.5) |
| [Voice](voice.md) | Bidirectional voice — voice as a regular agent thread | Draft (v0.5) |
| [Cortex](cortex.md) | Unifying device & connection layer — registration, hooks, commands, queries | Draft (v0.5) |

**Reading order:** interjection first — the voice and cortex documents both
consume that primitive and assume its vocabulary.

The three form one 0.5 arc: voice is the cortex's first streaming consumer, and
both ride the interjection primitive. A change to any contract in
[interjection-and-events.md](interjection-and-events.md) propagates to both
consumer documents.
