# Animus — Concept (NL)

## Index

- [Managementsamenvatting](#managementsamenvatting)
- [Concept](#concept)
- [Componenten](#componenten)
  - [Kern](#kern)
  - [Interfaces (extern)](#interfaces-extern)
  - [Connectors / secundaire systeemintegraties](#connectors--secundaire-systeemintegraties)
  - [Geheugenmodules](#geheugenmodules)
  - [FANN-“intuïtie”-integratie](#fann-intuïtie-integratie)
- [Technische schets](#technische-schets)
  - [Procesopzet (voorstel)](#procesopzet-voorstel)
  - [Talen](#talen)
  - [Hook-protocol](#hook-protocol)
  - [LLM-provider-adapters](#llm-provider-adapters)
  - [Prompttaxonomie / modeltoewijzing](#prompttaxonomie--modeltoewijzing)
  - [Infrastructuur / packaging](#infrastructuur--packaging)
- [Strategische positionering en de AI-race](#strategische-positionering-en-de-ai-race)
  - [Rationale](#rationale)
  - [Hoe het ontwerp de positionering ondersteunt](#hoe-het-ontwerp-de-positionering-ondersteunt)

## Managementsamenvatting

Animus is een gepland **agentic/agentisch AI-framework** dat bedoeld is als volledige vervanger van OpenClaw.

In gewone mensentaal: het is een systeem dat AI niet alleen laat “chatten”, maar ook **werk laat uitvoeren in echte tools** (Slack/Web/CLI; later: e-mail/tickets/CRM’s), terwijl het **controleerbaar, auditbaar en veilig** blijft.

Ontwerpintentie:

- Een **C++-kernel** die de cognitielus, prompt-samenstelling, geheugenbeleid en risicogates beheert.
- **Modulaire connectors** zodat Slack/Web/CLI (en later enterprise-systemen) op één abstractielaag aansluiten.
- **Multi-provider LLM-ondersteuning** plus **private/self-hosted inference** als first-class opties.
- Optionele **FANN-“instinct/intuïtie”-modules** voor numerieke subproblemen (advies-signalen, nooit autoriteit).

Wat dit betekent:
Agentische integratie is het verschil tussen **een chatbot iets vragen** en een **blijvende assistent die in je echte omgeving zit**. In plaats van tekst in een website te plakken, kan de assistent werken *waar het werk al gebeurt* (Slack, documenten, code, servers, interne tools): context lezen, concepten en documentatie opstellen, checks draaien, tickets aanmaken/updaten en workflows coördineren — met duidelijke approvals, permissies en audit trails.

- **Op kantoor:** management ziet een altijd-aan “operations brain” dat meetings omzet in briefings, briefings in tickets, tickets in uitlevering, en incidenten in gecoördineerde respons — terwijl mensen eindverantwoordelijk blijven.
- **Thuis:** een Jarvis-achtige assistent kan je devices en services coördineren (telefoon, agenda, auto, domotica), proactief voorbereiden wat je nodig hebt (samenvattingen, reminders, budgetten) en kleine integraties/scripts maken wanneer er een nieuw systeem verschijnt — zonder dat jij zelf de lijm hoeft te zijn.

---

## Concept

**Animus** is een gepland **agentic/agentisch AI-framework** dat bedoeld is als volledige vervanger van OpenClaw.

Het kernidee is dat we de *volledige cognitieve pijplijn* end-to-end zelf beheersen:

- **Prompt-samenstelling** als een expliciete, beleidsgestuurde “compileerstap” (niet als ad-hoc tekstplakken).
- **Geheugen** als een first-class systeem met strikte lees/schrijfregels (om contextvervuiling te verminderen).
- **Tools / integraties** als modulaire componenten, gekoppeld via één stabiele abstractielaag.
- Optionele, trainbare **“instinct/intuïtie”-modules** (FANN) voor *numerieke* subproblemen (advies, nooit autoriteit).

Animus moet zijn:

- **Efficiënt** (strakke budgetten, lage overhead, voorspelbare latency)
- **Capabel** (composable cognitielus; sterke integratiestory)
- **Veilig** (auditbaar gedrag, strikte schema’s, minimale ambient authority)
- **Context-hygiënisch** (vermijd irrelevante retrieval; reduceer prompt/context pollution)

## Componenten

### Kern

**C++ kernel / agent core**

Verantwoordelijkheden:

- Beheren van de **cognitielus** (waarnemen → ophalen → voorstellen → gate → uitvoeren → reflecteren → terugschrijven).
- Beheren van **prompt-samenstelling**:
  - promptcategorisering/taxonomie
  - contextbudgettering
  - deterministische “gates” (policy checks)
  - modelkeuze per categorie
- Beheren van **uitvoeringsgating** voor risicovolle acties (ask-first, safety budgets, redaction rules).
- Beheren van **logging / event trail** (auditbaar, append-only).

### Interfaces (extern)

Alle interfaces eindigen in één **gemeenschappelijke abstractielaag** (de kernel conversation/task API), zodat:

- geheugenbeleid gecentraliseerd is
- prompt-samenstelling consistent is
- gating rules uniform gelden

Initiële targets:

- **Slack** — primaire interface voor “serious work”.
- **Web interface** — admin console + interactieve UI.
- **CLI** — terminalstijl interface voor power users en scripting.

### Connectors / secundaire systeemintegraties

Connectors zijn *capability providers* (inputs, outputs en tools) die in de kernel worden “ingeplugd”.

Voorbeelden (initieel + logische vervolgopties):

- Slack-connector (berichten, threads, reacties, file attachments)
- Web UI backend-connector (sessies, streaming, inspectie)
- CLI-connector (stdin/stdout + lokale filesystem operations)
- Later: e-mail, kalender, GitHub/GitLab, ticketing, kennisbanken, on-prem enterprise systemen

Ontwerpintentie: connectors zijn modulair, least-privilege, en verwisselbaar.

### Geheugenmodules

Geheugen mag geen “één blob” zijn. Het moet lanes hebben met expliciete lees/schrijfregels.

Voorgestelde lanes (richtinggevend):

- **Event log (append-only)**: wat er gebeurde (messages, tool calls, outcomes); auditbare ground truth.
- **Working set**: korte-termijn “actieve context” voor lopende projecten.
- **Curated semantic memory**: feiten/beslissingen/relaties die duurzaam “waar” moeten blijven.
- **Optionele retrieval-accelerator**: embedding/vector index voor fuzzy recall (nooit als autoriteit beschouwd).

Belangrijke eigenschappen:

- promotie/demotie-beleid (“wat wordt duurzaam?”)
- traceerbaarheid (“waarom is dit opgehaald?”)
- harde budgetten (bytes/tokens/snippets) om vervuiling te beperken

### FANN-“intuïtie”-integratie

FANN-integratie is uitsluitend bedoeld voor **numerieke** subproblemen.

Principes:

- Instinct-uitkomsten zijn **advies-signalen**, geen eindbeslissingen.
- Hard gates (security/privacy/irreversibility) blijven deterministisch.
- Instinct-modules vereisen:
  - versiebeheer + rollbacks
  - evaluatie-harnas
  - feedbacksignalen / labeling-strategie

Goede kandidaat-taken:

- urgentiescore / notificatie-triage
- geheugenrouting (naar welke lane schrijven?)
- actieranking (kans-van-slagen, kostbewust)
- risicoschatting voor changes (bv. “cognitive debt” risk score)

## Technische schets

Dit is een eerste vorm; we verfijnen dit tijdens prototyping.

### Procesopzet (voorstel)

- **animusd** (C++): kernel daemon
  - Host de cognitielus, geheugenbeleid en tool/hook execution.
  - Expose een lokale API (bv. Unix socket HTTP, gRPC, of simpele JSON-RPC transport).

- **Connectors** (aparte processen/services):
  - Slack connector
  - Web UI backend
  - CLI client

Cruciaal: connectors zijn vervangbaar en praten met de kernel via een stabiel protocol.

### Talen

- **Core kernel**: C++ (primair)
- **Hooks / subsystemen**: Node/Python/Ruby (aangeroepen als terminal commands)
- **Web UI** (waarschijnlijk): TypeScript (frontend); backend kan dun zijn en delegeren naar kernel API

### Hook-protocol

Om “shell glue chaos” te vermijden, moeten hooks **gestructureerde JSON** gebruiken:

- Kernel stuurt JSON request op stdin
- Hook print JSON response op stdout
- Kernel handhaaft:
  - timeouts
  - maximale outputgrootte
  - schema-validatie
  - exit-code discipline
  - redaction-aware logging

### LLM-provider-adapters

We willen provider-adapters voor:

- **OpenAI**
- **Mistral** (nadere studie nodig)
- **Cohere** (nadere studie nodig)
- **GLM** (via Z.ai)
- **Aleph Alpha** (nadere studie nodig; mogelijk te weinig “powerful” afhankelijk van huidige stand)

We willen ook first-class support voor **private/self-hosted modellen** (lokaal, adjacent container/node, of remote private server). In de praktijk betekent dit waarschijnlijk een ondersteunende “LLM host”-component die een inference engine draait en een stabiele API aanbiedt die Animus kan behandelen als een normale provider.

Adapter-eisen:

- capability discovery (tool calling, JSON mode, streaming, context limits)
- consistente request/response-schema’s richting kernel
- modelkeuze per promptcategorie
- provider failover en policy-based fallback (optioneel)

### Prompttaxonomie / modeltoewijzing

Prompts moeten gecategoriseerd worden (voorbeelden):

- triage/routing (snel/goedkoop)
- deep planning (langzamer/sterker)
- code review
- samenvatten
- memory consolidation

Per categorie sturen we o.a.:

- welke geheugenlanes in aanmerking komen
- contextbudget
- tool-permissies
- welk model/provider te gebruiken
- logging-eisen (“why this model”, “why this context”)

### Infrastructuur / packaging

- Repo shipt met een **Dockerfile** zodat toolchain “ready-to-run” is.
- Voorkeur voor multi-stage builds (kleine runtime image).
- Secrets (provider keys, Slack tokens) alleen runtime-injectie (env/secret store), nooit in images bakken.

## Strategische positionering en de AI-race

### Rationale

Europese markten zoeken actief naar alternatieven voor Amerikaanse infrastructuur, en Europa is breed bezorgd over zijn positie in de “AI-race”.

Als Animus:

- **niet-US LLM-providers** geloofwaardig kan ondersteunen (met name **Mistral** (EU) en **Cohere** (Canada)),
- OpenClaw-achtige integratie kan bieden met extra focus op **efficiëntie, security en context-hygiëne**,
- “serious” connectors en workflows prioriteert (Slack/Web/CLI; enterprise-integraties) boven novelty/toy-integraties,

…dan kan het plausibel zijn voor inzet in meer conservatieve omgevingen.

### Hoe het ontwerp de positionering ondersteunt

- **Modulaire provider-adapters**: minder lock-in; regionale deployment-keuzes.
- **Expliciete cognitielus/prompt-compiler**: betere auditbaarheid en governance.
- **Matuur geheugensysteem**: minder accidental leakage en “context sprawl” in long-lived deployments.
- **C++ core**: performance + determinisme; past bij on-prem verwachtingen.
- **Least-privilege connectors + strikt hook-protocol**: sterker security-verhaal en makkelijker compliance.

Dit is geen belofte van funding; het is een plausibel *narrative wedge* als we iets bouwen dat in echte omgevingen vertrouwen verdient.
