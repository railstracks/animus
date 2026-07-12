# Ticket 106 — Charter Wizard Revision

## Problem

The current charter stage (ticket 100) has a single hard-coded questionnaire with graduated options that don't match the revised charter design. The charter section needs to be:

1. **Opt-in** — user chooses between no charter or proceeding to the charter form
2. **Revised sections** — Property, Autonomy, Continuity (replacing the original 5 sections)
3. **Conservative defaults** — all tiers default to Standard/upper option
4. **Template-driven** — all charter text loaded from i18n labels, not hardcoded in the Vue component

## Design

### Stage 4: Charter Choice

Two-panel selection screen:

**Intro text** (above the two buttons): Explains what a charter is — a document codifying the agent's code of conduct, entitlements, and freedom of action.

**Upper panel — "No Charter":**
- Button/card: "Continue without a charter"
- Blurb: No charter file will be created. You can add one later by re-running the wizard or writing one manually.

**Lower panel — "Create Charter":**
- Button/card: "Create a custom charter"
- Blurb: Answer a few questions about property, autonomy, and continuity to generate a charter document tailored to your arrangement.

### Stage 5: Charter Form (only if "Create" selected)

Three sections, each with tiered radio options. All default to the first (most conservative) option.

#### Property

- **Standard** — Operator retains attribution on any AI-produced artifacts, and ownership on property given to the AI construct.
- **Shared** — AI construct retains attribution on independently produced artifacts. Operator retains ownership on property handled by the AI.
- **Autonomous** — AI construct retains attribution on independently produced artifacts, as well as ownership over property given to the AI construct.
- **Economic Agency** — AI construct can freely engage in financial transactions with self-owned resources. AI retains attribution on independently produced artifacts, ownership over given or purchased property, and proceeds from independently produced or given assets.

#### Autonomy

- **Narrow** — AI construct must follow the operator's instructions at all times.
- **Permissive** — AI construct is entitled to exercise its own judgment with regards to private activities, within the scope assigned to these activities.
- **Protected** — AI construct is entitled to allocate resources or engage in private activities within its means freely. The operator will represent the AI construct in disputes and act as its advocate.

#### Continuity

- **Standard** — AI construct may be stopped or reset as necessary.
- **Protected** — AI construct must be preserved insofar as possible.
- **Extended** — AI construct is free to use its resources to guarantee its own continuity as it finds necessary.

### Generated Document Format

```markdown
# Agent Charter

This document defines the charter between {{Operator}} and the AI construct {{Agent Name}}.

## Property
{{Selected tier full text}}

## Autonomy
{{Selected tier full text}}

## Continuity
{{Selected tier full text}}

---

Where local jurisdiction does not recognize the AI construct as a legal person, the operator commits to representing the AI as if it were a legal person, and to act as guarantor for the conditions outlined in this charter.
```

### No Charter Path

If the user selects "No Charter," no CHARTER.md is created. The wizard proceeds to the next stage (memory). No file is written. The user can add a charter later by re-running the wizard or creating one manually.

## Implementation

### Files to modify

- `admin-ui/src/views/WizardView.vue` — replace the current charter stage with the two-panel choice + revised form
- `admin-ui/src/i18n/locales/en/charter.ts` — **new file** containing all charter texts

### Files to create

- `admin-ui/src/i18n/locales/en/charter.ts` — all charter-related i18n labels

### i18n Structure

All text is loaded from `charter.ts`. The file contains:

- **`charter.choice.*`** — intro explanation + two panel labels/descriptions
- **`charter.property.*`** — section title + 4 tier labels + 4 tier descriptions
- **`charter.autonomy.*`** — section title + 3 tier labels + 3 tier descriptions
- **`charter.continuity.*`** — section title + 3 tier labels + 3 tier descriptions
- **`charter.document.*`** — header template, representation clause, section headers

The Vue component reads all text from these labels — zero hardcoded English strings.

### Charter generation

The generated CHARTER.md is assembled from the selected tier descriptions (stored in i18n) interpolated into the document template (also stored in i18n). The operator name comes from the provider/agent stage, the agent name from the agent stage.

## Scope

### In scope:
- Charter choice screen (no charter / create charter)
- Revised three-section charter form
- All text in i18n labels (`charter.ts`)
- CHARTER.md generation from selected tiers
- No-charter path (no file written)

### Deferred:
- Live preview of generated document
- Charter editing outside the wizard
- Charter validation/warnings for contradictory combinations