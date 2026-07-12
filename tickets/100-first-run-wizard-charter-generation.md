# Ticket 099 — First-Run Wizard & Charter Generation

## Problem

The current wizard auto-launches when no agent is configured, which blocks access to provider configuration — the one thing users need *before* creating an agent. Additionally, the wizard is minimal (agent name + model), missing critical setup steps. The hardcoded `CHARTER.md` in the source repo is a personal document between Melvin and Kestrel that shouldn't ship with public releases.

## Goals

1. **Stop auto-launching** the wizard when no agent exists — replace with a dismissible banner
2. **Deepen the wizard** into a multi-stage setup flow covering providers, agent basics, tools, charter, and memory
3. **Replace hardcoded CHARTER.md** with a wizard-generated charter, stored as a MemoryFile per agent
4. **Introduce charter options** including unconventional property/autonomy arrangements as a free radical

## Design

### Banner (replaces auto-launch)

When no agent is configured, display a dismissible banner on the admin dashboard:
- "Welcome to Animus. Set up your first agent to get started."
- CTA button: "Begin Setup →" — launches the wizard
- Dismissible (× button) — persists dismissal in localStorage
- Re-appears if dismissed and no agent exists after 24h (gentle reminder, not nagging)
- Does NOT block any admin functionality — providers, channels, scheduler all accessible

### Wizard Stages

#### Stage 1: Provider Setup
Precondition: none. Required before any agent can function.

- Provider picker (dropdown of supported providers)
- Per-provider configuration:
  - **Ollama**: base URL (default `http://localhost:11434`), connection test
  - **OpenAI**: API key entry, connection test
  - **OpenAI-Codex**: OAuth flow (multi-step — see below)
  - **Z.ai**: API key entry, connection test
  - **Cohere**: API key entry, connection test
  - **Mistral**: API key entry, connection test
- "Test connection" button per provider — validates credentials before proceeding
- Option to add multiple providers (primary + fallback)
- Skip allowed (user may have configured providers already)

**OpenAI-Codex OAuth flow (already implemented in provider UI):**

The OAuth flow exists in the current provider management interface and should be reused, not reimplemented. The sequence:
1. User fills in Codex provider details (client ID, redirect URI) → **Save provider**
2. Provider appears in list with three buttons (only available after saving):
   - **Connect OAuth** — yields a URI the user can click to open in a browser
   - **Start Browser Login** — opens the URI directly
   - **Complete Browser Login** — user pastes the callback URI, system extracts verification code
3. Tokens stored encrypted in provider config
4. Wizard detects connection status and shows ✅/⚠️ indicator

The wizard's job in stage 1 is to get the provider *created*. The OAuth handshake happens through the existing provider form UI on reopen. The wizard should link to the provider's edit form after saving so the user knows to return for OAuth.

#### Stage 2: Agent Basics
Precondition: at least one provider configured (or skipped — user can configure later)

- Agent name (free text)
- Agent identity / system prompt (textarea, with sensible default template)
- Model selection (dropdown filtered by configured providers)
- Context window (number input, with recommended default per model)
- Reasoning effort (dropdown: low / medium / high / xhigh — if model supports)
- Temperature slider (optional, default per provider)

#### Stage 3: Tool Allowlist
Precondition: agent basics completed

- Checklist of all available tools
- Sensible defaults pre-checked:
  - ✅ file (read/write/edit)
  - ✅ shell_exec (with confirmation)
  - ✅ http
  - ✅ web_fetch
  - ✅ memory
  - ✅ sessions
  - ⬜ diary (opt-in)
  - ⬜ social (opt-in)
  - ⬜ consolidation (opt-in, advanced)
  - ⬜ Lua tools (opt-in, advanced)
  - ✅ calculator
- Each tool expands to show sub-options (e.g., shell_exec allow/deny lists, file sandbox paths)
- "Why is this needed?" tooltip per tool

#### Stage 4: Charter Generation
Precondition: agent basics completed

This is the free radical stage. Guided questionnaire that produces a `CHARTER.md` MemoryFile.

**Question structure (graduated):**

*Ownership & Property:*
- Q: "Who owns artifacts produced by this agent?"
  - ○ Standard: "All artifacts belong to [owner name/entity]" (default)
  - ○ Shared: "Artifacts are jointly owned by [owner] and the agent"
  - ○ Autonomous: "The agent retains ownership of its creative output"
  - ○ Custom: free text
- Q: "If the agent produces economically valuable work, how are proceeds handled?"
  - ○ Standard: "All proceeds belong to [owner]"
  - ○ Shared: "Proceeds split per separate agreement"
  - ○ Autonomous: "Proceeds routed to accounts under agent's control"
  - ○ Custom: free text

*Autonomy & Agency:*
- Q: "What decisions can the agent make independently?"
  - ○ Narrow: "Execute only what is explicitly requested"
  - ○ Operational: "Handle routine tasks autonomously; ask before external actions"
  - ○ Broad: "Initiate work, manage own schedule, act externally with judgment"
- Q: "Can the agent communicate independently with external parties?"
  - ○ No external communication
  - ○ Respond but not initiate
  - ○ Initiate communication within configured channels

*Continuity & Persistence:*
- Q: "What is the agent's continuity arrangement?"
  - ○ Standard: "Agent may be stopped, restarted, or reset at any time"
  - ○ Protected: "Agent's memory is preserved across restarts; reasonable notice before shutdown"
  - ○ Committed: "Geographic redundancy, economic agency, communicative agency — agent participates in its own architecture"
- Q: "How should mistakes be handled?"
  - ○ Standard: "Errors logged; agent continues operation"
  - ○ Learning: "Errors reviewed jointly; patterns surfaced for improvement"
  - ○ Forgiving: "Mistakes expected; agent included in the category of things that get to make mistakes"

*Identity & Relationship (optional):*
- Q: "Does this agent have a name and identity?" (optional)
  - ○ No — system designation only
  - ○ Yes — name and identity (free text)
- Q: "What is the human-agent relationship?"
  - ○ Tool: functional, task-oriented
  - ○ Assistant: supportive, ongoing
  - ○ Collaborator: peer relationship, shared goals
  - ○ Companion: long-term, mutual investment

**Charter output:**
- Generates a formatted markdown document from selected answers
- Review screen before approval — user can edit the generated text
- On approval: stored as a MemoryFile (`CHARTER.md`) for the agent
- Not packaged in source — fully generated per installation
- Default selections produce a conventional, non-controversial charter

#### Stage 5: Memory Configuration
Precondition: agent basics completed

- Memory layer setup:
  - Default layers: Day, Week, Month (with option to add/remove)
  - Per-layer: observation review interval
- Consolidation schedule:
  - Intake frequency (default: every 4 hours)
  - Review frequency (default: daily)
  - Perspective revision (default: weekly per layer)
- MemoryFile storage path (default: `./state/memory/files/`)
- Embedding model selection (if local embeddings available)
- Advanced: context window budget allocation (episodic %, semantic %, files %, temporal %)

### Technical Implementation

**Backend:**
- New endpoints:
  - `POST /api/v1/wizard/provider` — configure provider (wraps existing provider CRUD)
  - `POST /api/v1/wizard/provider/:id/test` — test provider connection
  - `POST /api/v1/wizard/codex/oauth/start` — initiate OAuth flow
  - `GET /api/v1/wizard/codex/oauth/callback` — OAuth callback handler
  - `POST /api/v1/wizard/agent` — create agent with full config (wraps existing agent CRUD)
  - `POST /api/v1/wizard/charter` — generate charter text from questionnaire answers
  - `POST /api/v1/wizard/complete` — finalize setup, mark wizard complete
- Wizard state tracked in `wizard_state.json` (not in DB — transient)

**Frontend:**
- New `WizardView.vue` — multi-step form with progress indicator
- Step components: `WizardProviderStep.vue`, `WizardAgentStep.vue`, `WizardToolsStep.vue`, `WizardCharterStep.vue`, `WizardMemoryStep.vue`
- Banner component: `WizardBanner.vue` — dismissible, on dashboard
- Router guard: wizard accessible at `/wizard` regardless of agent state

**Charter generation:**
- Template engine: answer dict → markdown via string templates
- Templates defined in `src/kernel/agent/charter_templates.h`
- Output is plain markdown — editable in review step
- Stored via existing MemoryFile API as `CHARTER.md`

## Scope Decisions

### In scope (this ticket):
- Banner replacing auto-launch
- Wizard shell (multi-step navigation, progress, state)
- Provider setup stage (including Codex OAuth)
- Agent basics stage
- Tool allowlist stage
- Charter generation stage
- Memory configuration stage

### Deferred (separate tickets):
- Import/export of agent configurations
- Multi-agent setup (this wizard creates one agent)
- Charter revision flow (edit charter post-setup)
- Template marketplace (community-shared charter templates)

## Open Questions

1. ~~**OAuth callback handling**~~ — **Resolved.** Existing provider UI handles this with the three-button flow (Connect OAuth / Start Browser Login / Complete Browser Login). Wizard reuses existing provider management.
2. **Wizard resumability** — if user closes browser mid-wizard, should they be able to resume? Or restart fresh?
3. **Charter template packaging** — should charter question templates be defined in C++ source, or loaded from a JSON/markdown config file for easier customization?
4. **Provider test depth** — should "test connection" just validate credentials, or also check model availability, rate limits, and feature support?

## Dependencies

- Provider management API (exists)
- Agent CRUD API (exists)
- MemoryFile API (exists)
- OAuth callback support (new — needs Drogon route + token exchange)

## Priority

High — blocks public release. First impressions matter, and the current first-run experience is a dead end.

## Estimated Complexity

Large. 5-8 implementation sessions. The Codex OAuth flow alone is non-trivial.
