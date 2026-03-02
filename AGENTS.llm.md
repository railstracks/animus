# AGENTS.llm.md — LLM Provider Scaffold

> Status: **Exploratory scaffold**.
>
> This document sketches candidate LLM provider integrations for Animus.
> The implementation details are intentionally undefined here; these sections exist to anchor later design and iteration.

---

## Zai

Zai would document how Animus might integrate with Z.ai and GLM-family models, including client configuration, authentication, and model capability mapping. This section can later capture provider-specific constraints around context windows, streaming, and tool use semantics.

### Placeholder

- Outline API surface, auth flow, and model selection for Z.ai or GLM deployments
- Capture any provider-specific behavior that affects prompt assembly, streaming, or structured outputs

---

## Mistral

Mistral would document how Animus might integrate with Mistral AI models and endpoints, including request formatting, hosted model options, and operational limits. This section can later define how Mistral-specific features map onto Animus chain execution and response handling.

### Placeholder

- Define configuration, credentials, and supported model families for Mistral AI
- Note any special handling for streaming, function calling, or tokenizer and context constraints

---

## Cohere

Cohere would document how Animus might integrate with Cohere generation and reasoning models, including authentication, parameter translation, and runtime behavior. This section can later describe provider-specific support for retrieval, tool use, and structured generation workflows.

### Placeholder

- Describe client setup and parameter mapping for Cohere model requests
- Record capabilities or limitations relevant to retrieval, tool use, and structured outputs

---

## Aleph

Aleph would document how Animus might integrate with Aleph Alpha services, including endpoint selection, credentials, and model capability alignment. This section can later cover any provider-specific behaviors that affect prompt packaging, inference controls, and response parsing.

### Placeholder

- Identify integration points, auth requirements, and supported Aleph Alpha models
- Track any differences in inference controls, response formats, or deployment assumptions

---

## local

Local would document how Animus might integrate with self-hosted or on-device models, whether through direct runtimes, local gateways, or OpenAI-compatible proxy layers. This section can later define expectations for model discovery, hardware constraints, and operational safeguards in local deployments.

### Placeholder

- Explore local runtime patterns such as direct model execution or API-compatible gateway access
- Clarify assumptions around hardware, latency, observability, and security boundaries for self-hosted models
