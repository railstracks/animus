# AGENTS.memory.md — Memory Module Scaffold

> Status: **Exploratory scaffold**.
>
> This document sketches candidate memory modules for Animus.
> The architecture is intentionally undefined here; these sections exist to anchor later design and iteration.

---

## Episodic Memory

Episodic memory would hold event- and experience-oriented records from prior chains, sessions, and tool executions. This is the likely home for chain traces, notable outcomes, and retrieval of "what happened before" with provenance.

### Placeholder

- Capture and index meaningful events from session and chain execution
- Support time-ordered retrieval, summaries, and provenance back to source events

---

## Ontology

Ontology would represent durable entities and relationships that the agent can reason over across sessions. This is the likely home for structured knowledge about users, projects, systems, concepts, and how they connect.

### Placeholder

- Model entities, attributes, and links between them
- Support retrieval and updates with clear confidence and source metadata

---

## Interiority

Interiority would persist agent-facing self-model data such as identity, preferences, commitments, and reflective state. This is the likely home for continuity of persona and longer-lived self-reflection across activations.

### Placeholder

- Store stable identity context and evolving self-reflective notes
- Define boundaries for what can be persisted versus recomputed per chain

---

## Intuition

Intuition would cover fast, pattern-oriented heuristics that help the agent recognize familiar situations without full deliberation. This is the likely home for lightweight signals, learned shortcuts, and ranking cues that bias retrieval or action selection.

### Placeholder

- Explore compact pattern representations and retrieval-time heuristics
- Clarify how intuition informs decisions without obscuring provenance or policy controls
