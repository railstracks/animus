# AGENTS.tenancy.md — Tenancy Scaffold

> Status: **Exploratory scaffold**.
>
> This document sketches candidate tenancy patterns for Animus.
> The architecture is intentionally undefined here; these sections exist to anchor later design and iteration.

---

## Multi-agent tenancy

Multi-agent tenancy would describe how multiple agents can coexist within the same kernel while sharing selected context, infrastructure, and runtime resources. This section can later define isolation boundaries, shared services, and the rules for cross-agent visibility.

### Placeholder

- Define how agents share or isolate sessions, memory, tools, and budgets
- Clarify tenancy boundaries for security, observability, and operational control

---

## Sub-agents

Sub-agents would describe delegation patterns where one agent can invoke or coordinate specialized child agents for narrower tasks. This section can later capture lifecycle rules, context handoff, and how sub-agent outputs are returned to the calling agent.

### Placeholder

- Explore delegation, task scoping, and result handoff between parent and child agents
- Document how permissions, memory access, and execution budgets flow to sub-agents

---

## Hierarchies

Hierarchies would describe parent-child agent relationships, authority models, and supervision rules across multi-agent structures. This section can later define control flow, escalation paths, and how responsibility is assigned across layered agent systems.

### Placeholder

- Describe authority, override, and escalation rules in agent hierarchies
- Clarify how hierarchy structure affects routing, policy enforcement, and audit trails
