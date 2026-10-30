#include "animus_kernel/consolidation/ConsolidationPipeline.h"
#include "animus_kernel/IDataStore.h"
#include "animus_kernel/Log.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/TokenEstimate.h"
#include "animus_kernel/scheduler/Scheduler.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>

namespace animus::kernel {

namespace {

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// #219: capture the current observation population of a layer as
// id -> Observation, for diffing before/after an LLM callback. The
// intake functions already count created observations via a before/after
// diff; this makes the created rows themselves capturable so the
// reconciler receives the actual evidence it must link properties to.
std::unordered_map<int64_t, memory::Observation> SnapshotLayerObservations(
    memory::MemoryStore* memoryStore, int64_t layerId) {
    std::unordered_map<int64_t, memory::Observation> out;
    if (!memoryStore || layerId <= 0) return out;
    for (const auto& obs : memoryStore->ListObservationsForLayer(layerId)) {
        out.emplace(obs.id, obs);
    }
    return out;
}

// #219: push rows present after but absent before the callback window into
// the reconciler's evidence vector. Returns the count pushed (the same
// rows the intake functions already count as `created`).
int64_t CollectCreatedObservations(
    const std::unordered_map<int64_t, memory::Observation>& before,
    const std::unordered_map<int64_t, memory::Observation>& after,
    std::vector<memory::Observation>* out) {
    if (!out) return 0;
    int64_t pushed = 0;
    for (const auto& [id, obs] : after) {
        if (before.count(id) == 0) {
            out->push_back(obs);
            ++pushed;
        }
    }
    return pushed;
}

// #224 drive-by: the session-report schedule is documented as staggered
// 30 minutes after intake. Shift a cron expression's minute field by +30
// (mod 60) — but ONLY for a plain integer minute field. Step ("*/5"),
// list ("0,30"), and range ("10-50") minute fields are left untouched:
// shifting those changes the schedule's shape, not just its phase, and
// for step expressions the +30 heuristic doesn't even apply cleanly.
// Returns the input unchanged when no shift is applied.
std::string ShiftCronMinuteBy30(const std::string& cron) {
    // Five-field cron expected; anything else passes through untouched.
    const size_t firstSpace = cron.find(' ');
    if (firstSpace == std::string::npos || cron.find_first_not_of(" \t") != 0) {
        return cron;
    }
    const std::string minuteField = cron.substr(0, firstSpace);
    // Plain non-negative integer only — no '*', '/', ',', '-', leading spaces.
    if (minuteField.empty() ||
        minuteField.find_first_not_of("0123456789") != std::string::npos) {
        return cron;
    }
    int minute = 0;
    try {
        minute = std::stoi(minuteField);
    } catch (...) {
        return cron;  // overflow or parse failure — leave untouched
    }
    if (minute < 0 || minute > 59) return cron;  // not a valid minute field
    const int shifted = (minute + 30) % 60;
    return std::to_string(shifted) + cron.substr(firstSpace);
}

// Extract JSON array from LLM response (handles markdown code blocks)
std::string ExtractJsonFromResponse(const std::string& response) {
    // Try to find ```json ... ``` block
    auto start = response.find("```json");
    if (start != std::string::npos) {
        start = response.find('\n', start);
        if (start != std::string::npos) start++;
        auto end = response.find("```", start);
        if (end != std::string::npos) {
            return response.substr(start, end - start);
        }
    }
    // Try to find ``` ... ``` block
    start = response.find("```");
    if (start != std::string::npos) {
        start = response.find('\n', start);
        if (start != std::string::npos) start++;
        auto end = response.find("```", start);
        if (end != std::string::npos) {
            return response.substr(start, end - start);
        }
    }
    // Try to find raw JSON array
    start = response.find('[');
    if (start != std::string::npos) {
        // Find matching closing bracket
        int depth = 0;
        for (size_t i = start; i < response.size(); ++i) {
            if (response[i] == '[') depth++;
            else if (response[i] == ']') {
                depth--;
                if (depth == 0) {
                    return response.substr(start, i - start + 1);
                }
            }
        }
    }
    // Try to find raw JSON object
    start = response.find('{');
    if (start != std::string::npos) {
        int depth = 0;
        for (size_t i = start; i < response.size(); ++i) {
            if (response[i] == '{') depth++;
            else if (response[i] == '}') {
                depth--;
                if (depth == 0) {
                    return response.substr(start, i - start + 1);
                }
            }
        }
    }
    return response;
}

Json::Value ParseJson(const std::string& json) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(json);
    std::string errors;
    Json::parseFromStream(builder, stream, &root, &errors);
    return root;
}

std::string JsonToString(const Json::Value& value) {
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, value);
}

} // anonymous namespace

// ============================================================================
// Construction + lifecycle
// ============================================================================

ConsolidationPipeline::ConsolidationPipeline(
        IDataStore* dataStore,
        memory::MemoryStore* memoryStore,
        ontology::OntologyStore* ontologyStore,
        DiaryStore* diaryStore,
        SessionManager* sessionManager,
        AgentStore* agentStore,
        ConsolidationLLMCallback llmCallback)
    : m_store(dataStore)
    , m_memoryStore(memoryStore)
    , m_ontologyStore(ontologyStore)
    , m_diaryStore(diaryStore)
    , m_sessionManager(sessionManager)
    , m_agentStore(agentStore)
    , m_llmCallback(std::move(llmCallback)) {
}

ConsolidationPipeline::~ConsolidationPipeline() {
    Stop();
}

void ConsolidationPipeline::Configure(const Config& config) {
    m_config = config;
}

ConsolidationPipeline::Config ConsolidationPipeline::GetConfig() const {
    return m_config;
}

bool ConsolidationPipeline::Start(Scheduler* scheduler, std::string* error) {
    if (m_running.load()) return true;
    m_scheduler = scheduler;
    RegisterSchedules(scheduler);
    m_running.store(true);
    return true;
}

void ConsolidationPipeline::Stop() {
    if (!m_running.load()) return;
    // Cancel all registered cron schedules
    if (m_scheduler) {
        for (const auto& id : m_registeredScheduleIds) {
            std::string err;
            m_scheduler->Cancel(id, &err);
        }
        m_registeredScheduleIds.clear();
    }
    m_running.store(false);
}

bool ConsolidationPipeline::IsRunning() const {
    return m_running.load();
}

// ============================================================================
// Scheduler integration — registers cron jobs for all enabled layers
// ============================================================================

void ConsolidationPipeline::RegisterSchedules(Scheduler* scheduler) {
    if (!scheduler || !m_memoryStore) return;

    // ---- Deduplicate: remove stale consolidation schedules from previous runs ----
    ClearStaleConsolidationSchedules(scheduler);

    // Register intake schedules from agent-level config or layer-level config.
    // Agent-level takes precedence; falls back to any layer with intake_interval set.
    if (m_config.intake_enabled && m_agentStore) {
        for (const auto& agent : m_agentStore->List()) {
            std::string intakeCron = agent.intake_interval;
            std::string intakeLayer = "day";  // default layer for intake

            // If agent-level interval is empty, check layers for one
            if (intakeCron.empty() && m_memoryStore) {
                for (const auto& layer : m_memoryStore->ListLayersForAgent(agent.id)) {
                    if (layer.intake_interval.has_value() && !layer.intake_interval->empty()) {
                        intakeCron = *layer.intake_interval;
                        intakeLayer = layer.name;
                        break;
                    }
                }
            }
            if (intakeCron.empty()) continue;

            ScheduleDescriptor intakeSd;
            intakeSd.agent_id = agent.id;
            intakeSd.tag = "consolidation";
            intakeSd.type = ScheduleType::Recurring;
            intakeSd.next_fire = intakeCron;
            intakeSd.message = "intake:" + intakeLayer;

            std::string err;
            std::string id = scheduler->Create(intakeSd, &err);
            if (!id.empty()) {
                m_registeredScheduleIds.push_back(id);
                std::cerr << "[consolidation] registered intake schedule for agent "
                          << agent.id << " (cron=" << intakeCron << ", layer=" << intakeLayer << ")" << std::endl;
            } else {
                std::cerr << "[consolidation] failed to register intake schedule for agent "
                          << agent.id << ": " << err << std::endl;
            }
        }
    }

    auto layers = m_memoryStore->ListLayers();
    for (const auto& layer : layers) {
        if (!layer.enabled) continue;
        if (layer.name == "millennium") continue;

        if (layer.cron_expr.empty()) continue;
        ScheduleDescriptor sd;
        sd.agent_id = layer.agent_id;
        sd.tag = "consolidation";
        sd.type = ScheduleType::Recurring;
        sd.next_fire = layer.cron_expr;  // Scheduler::Create detects cron from next_fire
        sd.message = "consolidate:" + layer.name;

        std::string err;
        std::string id = scheduler->Create(sd, &err);
        if (!id.empty()) {
            m_registeredScheduleIds.push_back(id);
            std::cerr << "[consolidation] registered review schedule for layer "
                      << layer.name << " (agent=" << layer.agent_id
                      << ", cron=" << layer.cron_expr << ")" << std::endl;
        } else {
            std::cerr << "[consolidation] failed to register schedule for layer "
                      << layer.name << " (agent=" << layer.agent_id << "): " << err << std::endl;
        }
    }

    // Register session-reporting schedules for each agent.
    // Uses the same intake interval (reports should be generated shortly after intake).
    // Staggered 30 minutes after intake to avoid competing for resources.
    if (m_config.intake_enabled && m_agentStore) {
        for (const auto& agent : m_agentStore->List()) {
            std::string reportCron = agent.intake_interval;
            if (reportCron.empty()) {
                // Check layers for an interval
                if (m_memoryStore) {
                    for (const auto& layer : m_memoryStore->ListLayersForAgent(agent.id)) {
                        if (layer.intake_interval.has_value() && !layer.intake_interval->empty()) {
                            reportCron = *layer.intake_interval;
                            break;
                        }
                    }
                }
            }
            if (reportCron.empty()) continue;

            ScheduleDescriptor reportSd;
            reportSd.agent_id = agent.id;
            reportSd.tag = "consolidation";
            reportSd.type = ScheduleType::Recurring;
            // Offset by 30 minutes: shift cron minute by +30 (mod 60).
            // Applied only to plain integer minute fields — step/list/range
            // minute fields pass through untouched (see ShiftCronMinuteBy30).
            // Sub-hourly crons may still overlap with intake; the shift is a
            // heuristic for clean separation on typical hourly/daily schedules.
            reportSd.next_fire = ShiftCronMinuteBy30(reportCron);
            reportSd.message = "session_report";

            std::string err;
            std::string reportId = scheduler->Create(reportSd, &err);
            if (!reportId.empty()) {
                m_registeredScheduleIds.push_back(reportId);
                std::cerr << "[consolidation] registered session_report schedule for agent "
                          << agent.id << " (cron=" << reportSd.next_fire << ")" << std::endl;
            } else {
                std::cerr << "[consolidation] failed to register session_report schedule for agent "
                          << agent.id << ": " << err << std::endl;
            }
        }
    }
}

// ============================================================================
// Stale-schedule cleanup — shared by Start and the #224 refresh path
// ============================================================================

void ConsolidationPipeline::ClearStaleConsolidationSchedules(Scheduler* scheduler) {
    // Clear ALL consolidation schedules from any previous daemon run.
    // Old schedules may have different agent_ids ("system", "default", or real UUIDs)
    // and their next_fire times are in the past, causing immediate firing on boot.
    // #224: the clear set must cover every agent that can own a consolidation
    // schedule — layer agent ids too, not just AgentStore rows. A layer created
    // for an agent id that never got an AgentStore row (admin-created bare ids)
    // would otherwise keep its stale schedule across refreshes and duplicate.
    std::vector<std::string> agentsToClear;
    agentsToClear.push_back("system");
    agentsToClear.push_back("default");
    if (m_agentStore) {
        for (const auto& a : m_agentStore->List()) agentsToClear.push_back(a.id);
    }
    if (m_memoryStore) {
        for (const auto& layer : m_memoryStore->ListLayers()) {
            if (std::find(agentsToClear.begin(), agentsToClear.end(), layer.agent_id)
                == agentsToClear.end()) {
                agentsToClear.push_back(layer.agent_id);
            }
        }
    }
    size_t totalCleared = 0;
    for (const auto& aid : agentsToClear) {
        auto existing = scheduler->List(aid, "consolidation");
        for (const auto& sched : existing) {
            std::string err;
            scheduler->Cancel(sched.id, &err);
        }
        totalCleared += existing.size();
    }
    if (totalCleared > 0) {
        std::cerr << "[consolidation] cleared " << totalCleared
                  << " stale consolidation schedules across " << agentsToClear.size()
                  << " agents" << std::endl;
    }
}

// ============================================================================
// #224: live schedule refresh — layers added/updated/enabled while running
// ============================================================================

void ConsolidationPipeline::RefreshSchedules() {
    // No-op unless running: before Start there is nothing to refresh, and
    // after Stop the m_registeredScheduleIds are already cancelled. The guard
    // also keeps a late layer-change callback (racing teardown) inert.
    if (!m_running.load()) return;
    if (!m_scheduler) return;
    RegisterSchedules(m_scheduler);
}

// ============================================================================
// GetKnownAgents — discover agents that have memory data
// ============================================================================

std::vector<std::string> ConsolidationPipeline::GetKnownAgents() const {
    std::unordered_map<std::string, bool> agentSet;

    // From agent store (primary source of truth)
    if (m_agentStore) {
        for (const auto& agent : m_agentStore->List()) {
            agentSet[agent.id] = true;
        }
    }

    // From memory observations (fallback)
    if (m_memoryStore) {
        const auto layers = m_memoryStore->ListLayers();
        for (const auto& layer : layers) {
            const auto obs = m_memoryStore->ListObservations(layer.id);
            for (const auto& o : obs) {
                if (!o.agent_id.empty()) agentSet[o.agent_id] = true;
            }
        }
    }

    // From diary entries
    if (m_diaryStore) {
        // Get a broad set by querying recent entries
        // DiaryStore::ListByAgent requires an agentId, so we check
        // common defaults + any agent that has observations
        std::vector<std::string> toCheck;
        for (const auto& [id, _] : agentSet) {
            toCheck.push_back(id);
        }
        for (const auto& agentId : toCheck) {
            auto entries = m_diaryStore->ListByAgent(agentId, NowMs() - 86400000);
            if (!entries.empty()) {
                agentSet[agentId] = true;
            }
        }
    }

    std::vector<std::string> result;
    for (const auto& [id, _] : agentSet) {
        result.push_back(id);
    }
    return result;
}

// ============================================================================
// HasPendingIntakeData — check if there is unprocessed data for intake
// ============================================================================

bool ConsolidationPipeline::HasPendingIntakeData(const std::string& agentId) {
    // Check for unprocessed session turns via DB query.
    // Must use the same data source as IntakeFromSessions (GetUnprocessedTurns)
    // rather than in-memory sessions, which may be incomplete after a restart.
    if (m_sessionManager) {
        auto pending = m_sessionManager->GetUnprocessedTurns(agentId, 1);
        if (!pending.empty()) return true;
    }

    // Check for unprocessed MemoryFiles
    if (m_memoryFileStore) {
        int64_t agentNum = 0;
        if (m_agentStore && !agentId.empty()) {
            auto agent = m_agentStore->GetById(agentId);
            if (agent) agentNum = agent->numeric_id;
        }
        if (agentNum == 0) {
            try { agentNum = std::stoll(agentId); } catch (...) {}
        }
        int64_t unprocessed = m_memoryFileStore->CountUnprocessed(agentNum);
        if (unprocessed > 0) return true;
    }

    return false;
}

bool ConsolidationPipeline::HasAnyPendingIntakeData() {
    // Check all known agents for pending intake data.
    // Used by the scheduler gate when the schedule's agent_id is "system" or
    // otherwise doesn't match a real agent.
    auto agents = GetKnownAgents();
    for (const auto& agentId : agents) {
        if (HasPendingIntakeData(agentId)) return true;
    }
    return false;
}

// ============================================================================
// Intake — scan unprocessed activity and create observations
// ============================================================================

bool ConsolidationPipeline::RunIntake(
        const std::string& agentId,
        std::optional<int64_t> targetLayerId,
        std::string* error) {
    ConsolidationRun run;
    run.agent_id = agentId;
    run.phase = "intake";
    run.started_unix_ms = NowMs();
    run.status = "running";
    const int64_t runId = m_store.CreateRun(run);

    std::optional<memory::MemoryLayer> targetLayer;
    if (targetLayerId.has_value()) {
        targetLayer = m_memoryStore->GetLayer(*targetLayerId);
        if (!targetLayer.has_value()) {
            m_store.FinishRun(runId, "failed", "{}", "target intake layer not found");
            if (error) *error = "target intake layer not found";
            return false;
        }
    } else {
        targetLayer = m_memoryStore->GetIntakeLayer(agentId);
        if (!targetLayer.has_value()) {
            targetLayer = m_memoryStore->GetLowestLayer(agentId);
        }
        if (!targetLayer.has_value()) {
            {
                std::lock_guard<std::mutex> lock(m_statsMutex);
                m_stats.runs_completed++;
            }
            m_store.FinishRun(
                runId,
                "completed",
                "{\"observations_created\":0,\"reason\":\"no memory layers configured\"}",
                "");
            return true;
        }
    }

    int64_t totalCreated = 0;
    std::vector<memory::Observation> createdObservations;
    createdObservations.reserve(static_cast<std::size_t>(m_config.intake_batch_size));

    // Intake from diary entries
    std::string diaryErr;
    totalCreated += IntakeFromDiary(
        agentId, targetLayer->id, &createdObservations, &diaryErr);

    // Intake from session turns
    std::string sessionErr;
    totalCreated += IntakeFromSessions(
        agentId, targetLayer->id, &createdObservations, &sessionErr);

    // Phase 2: Ontology reconciliation from freshly-created observations.
    std::string ontologyErr;
    const int64_t ontologyUpdates = ReconcileOntologyFromObservations(
        agentId, targetLayer->id, createdObservations, &ontologyErr);

    // Update stats
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.observations_created += totalCreated;
        m_stats.runs_completed++;
    }

    Json::Value summary(Json::objectValue);
    summary["target_layer_id"] = static_cast<Json::Int64>(targetLayer->id);
    summary["target_layer_name"] = targetLayer->name;
    summary["observations_created"] = static_cast<Json::Int64>(totalCreated);
    summary["ontology_updates"] = static_cast<Json::Int64>(ontologyUpdates);
    summary["diary_error"] = diaryErr;
    summary["session_error"] = sessionErr;
    summary["ontology_error"] = ontologyErr;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    m_store.FinishRun(runId, "completed", Json::writeString(wb, summary), "");

    return true;
}

int64_t ConsolidationPipeline::IntakeFromDiary(
        const std::string& agentId,
        int64_t targetLayerId,
        std::vector<memory::Observation>* createdObservations,
        std::string* error) {
    if (!m_diaryStore) return 0;

    // Get watermark
    auto watermark = m_store.GetWatermark(agentId, "diary_entries");
    const int64_t sinceMs = watermark ? watermark->last_run_unix_ms : 0;

    // Fetch unprocessed diary entries
    auto entries = m_diaryStore->ListByAgent(agentId, sinceMs, 0, "",
                                              m_config.intake_batch_size);
    if (entries.empty()) return 0;

    auto targetLayer = m_memoryStore->GetLayer(targetLayerId);
    if (!targetLayer) {
        if (error) *error = "target intake layer not found";
        return 0;
    }

    // #219: snapshot the layer before the LLM run — the before/after diff
    // yields both the created count and the created rows themselves
    // (reconciler evidence).
    const auto obsBefore = SnapshotLayerObservations(m_memoryStore, targetLayer->id);

    // Build the prompt
    std::ostringstream userPrompt;
    userPrompt << "Agent: " << agentId << "\n\n";
    userPrompt << "Recent diary entries:\n\n";
    for (const auto& entry : entries) {
        userPrompt << "[" << entry.layer << "] " << entry.content << "\n";
        if (!entry.tags_json.empty() && entry.tags_json != "[]") {
            userPrompt << "  Tags: " << entry.tags_json << "\n";
        }
        userPrompt << "\n";
    }
    userPrompt << "\nReview the entries above. For each lasting observation worth keeping, "
               << "call the consolidation tool with action \"create\" and provide "
               << "params: {text, tags, weight, layer, agent_id}.\n";

    std::string intakePrompt = m_config.intake_prompt;
    // Agent-level intake prompt overrides the global default
    if (m_agentStore) {
        auto agent = m_agentStore->GetById(agentId);
        if (agent && !agent->intake_prompt.empty()) {
            intakePrompt = agent->intake_prompt;
        }
    }
    // Fall back to layer-level prompt for backward compatibility
    if (intakePrompt == m_config.intake_prompt && targetLayer && !targetLayer->consolidation_intake_prompt.empty()) {
        intakePrompt = targetLayer->consolidation_intake_prompt;
    }

    // Call LLM — the agent creates observations via tool calls
    try {
        m_llmCallback(agentId, intakePrompt, userPrompt.str());
    } catch (const std::exception& e) {
        if (error) *error = std::string("LLM callback failed: ") + e.what();
        return 0;
    }

    // Count observations after — the agent created them via consolidation.create
    const auto obsAfter = SnapshotLayerObservations(m_memoryStore, targetLayer->id);
    const int64_t created = CollectCreatedObservations(obsBefore, obsAfter, createdObservations);

    // Update watermark
    ConsolidationWatermark wm;
    wm.agent_id = agentId;
    wm.source = "diary_entries";
    wm.last_processed_id = entries.back().timestamp_unix_ms;
    wm.last_run_unix_ms = NowMs();
    m_store.SetWatermark(wm);

    return created;
}

int64_t ConsolidationPipeline::IntakeFromSessions(
        const std::string& agentId,
        int64_t targetLayerId,
        std::vector<memory::Observation>* createdObservations,
        std::string* error) {
    if (!m_sessionManager) return 0;

    // Resolve intake prompt (agent-level → layer-level → global default)
    auto targetLayer = m_memoryStore->GetLayer(targetLayerId);
    std::string intakePrompt = m_config.intake_prompt;
    if (m_agentStore) {
        auto agent = m_agentStore->GetById(agentId);
        if (agent && !agent->intake_prompt.empty()) {
            intakePrompt = agent->intake_prompt;
        }
    }
    if (intakePrompt == m_config.intake_prompt && targetLayer && !targetLayer->consolidation_intake_prompt.empty()) {
        intakePrompt = targetLayer->consolidation_intake_prompt;
    }

    // Resolve context window for token budgeting.
    // Default to 128K if no resolver is wired (conservative).
    std::size_t contextWindow = 128000;
    if (m_contextWindowResolver) {
        contextWindow = m_contextWindowResolver(agentId);
    }
    // Use 50% of context window for intake batches — leaves room for system prompt,
    // tool definitions, and the LLM's response (tool calls to create observations).
    const std::size_t batchTokenBudget = contextWindow / 2;

    int64_t totalCreated = 0;

    // Process unprocessed turns in sequential batches, each fitting within
    // the token budget. This ensures we stay current with memory consolidation
    // even when many turns have accumulated.
    while (true) {
        // Fetch a generous pool of pending turns (we'll select from these by token budget)
        auto pending = m_sessionManager->GetUnprocessedTurns(
            agentId, m_config.intake_batch_size);

        if (pending.empty()) break;

        // Accumulate turns into the prompt until we hit the token budget
        std::ostringstream userPrompt;
        userPrompt << "Agent: " << agentId << "\n\n";
        userPrompt << "Recent session turns:\n\n";

        std::vector<SessionTurnId> batchIds;
        std::size_t batchTokens = 0;
        std::size_t batchCount = 0;

        for (const auto& turn : pending) {
            std::size_t turnTokens = turn.token_count;
            if (turnTokens == 0) {
                turnTokens = TokenEstimate::Estimate(turn.content);
            }
            // +4 for message overhead (role tag, newlines)
            turnTokens += 4;

            // If adding this turn would exceed budget and we already have turns,
            // stop here and process this batch. The remaining turns will be
            // picked up in the next iteration of the outer loop.
            if (batchCount > 0 && batchTokens + turnTokens > batchTokenBudget) {
                break;
            }

            userPrompt << "[" << turn.role << "] " << turn.content << "\n\n";
            batchIds.push_back(turn.turn_id);
            batchTokens += turnTokens;
            ++batchCount;
        }

        if (batchCount == 0) {
            // Single turn exceeds budget — process it alone to avoid infinite loop
            const auto& turn = pending[0];
            userPrompt << "[" << turn.role << "] " << turn.content << "\n\n";
            batchIds.push_back(turn.turn_id);
            batchCount = 1;
        }

        userPrompt << "\nReview the turns above. For each lasting observation worth keeping, "
                   << "call the consolidation tool with action \"create\" and provide "
                   << "params: {text, tags, weight, layer, agent_id}.\n";

        // #219: snapshot before the LLM run — the batch diff yields both
        // the created count and the created rows (reconciler evidence).
        const auto obsBefore = SnapshotLayerObservations(
            m_memoryStore, targetLayer ? targetLayer->id : 0);

        // Send batch to LLM
        std::string llmResponse;
        try {
            llmResponse = m_llmCallback(agentId, intakePrompt, userPrompt.str());
        } catch (const std::exception& e) {
            if (error) *error = std::string("LLM callback failed: ") + e.what();
            // Don't mark turns as processed on failure — they'll be retried next cycle
            break;
        }

        // Count observations created by this batch — the diff against the
        // pre-callback snapshot yields both the count and the created rows.
        const auto obsAfter = SnapshotLayerObservations(
            m_memoryStore, targetLayer ? targetLayer->id : 0);
        totalCreated += CollectCreatedObservations(obsBefore, obsAfter, createdObservations);

        // Mark this batch's turns as processed
        m_sessionManager->MarkTurnsProcessed(batchIds);

        // If we processed fewer turns than we fetched, the remaining unprocessed
        // turns will be picked up in the next outer-loop iteration.
        // If we processed all fetched turns and there might be more in the DB,
        // the loop continues and fetches the next batch.
        if (batchCount < pending.size()) {
            // We hit the token budget before exhausting the pool — continue to next batch
            continue;
        }
        // We processed all fetched turns. If the DB had exactly intake_batch_size
        // turns, there might be more. If it had fewer, we're done.
        if (static_cast<int>(pending.size()) < m_config.intake_batch_size) {
            break;
        }
    }

    return totalCreated;
}

int64_t ConsolidationPipeline::ReconcileOntologyFromObservations(
        const std::string& agentId,
        int64_t targetLayerId,
        const std::vector<memory::Observation>& observations,
        std::string* error) {
    if (!m_ontologyStore || observations.empty()) return 0;

    // #219: the evidence set — observation IDs this run may link against.
    // The model may echo an ID verbatim, hallucinate one, or emit it as a
    // string; anything that doesn't resolve to a real observation must not
    // reach SetProperty (a missing observation deprecates the property —
    // evidence poisoning).
    std::unordered_set<int64_t> validObservationIds;
    for (const auto& obs : observations) {
        validObservationIds.insert(obs.id);
    }

    std::ostringstream userPrompt;
    userPrompt << "Agent: " << agentId << "\n\n";
    userPrompt << "Fresh observations for ontology reconciliation:\n\n";
    for (const auto& obs : observations) {
        userPrompt << "ID " << obs.id << ": " << obs.text << "\n";
    }
    userPrompt << "\nReturn ontology updates as JSON.\n";

    std::string prompt = m_config.ontology_prompt;
    if (auto layer = m_memoryStore->GetLayer(targetLayerId);
        layer && !layer->consolidation_intake_prompt.empty()) {
        // Keep layer-specific intent in scope for ontology gardening.
        prompt += "\n\nIntake layer guidance:\n" + layer->consolidation_intake_prompt;
    }

    std::string llmResponse;
    try {
        llmResponse = m_llmCallback(agentId, prompt, userPrompt.str());
    } catch (const std::exception& e) {
        if (error) *error = std::string("LLM callback failed: ") + e.what();
        return 0;
    }

    auto parsed = ParseJson(ExtractJsonFromResponse(llmResponse));
    Json::Value upserts(Json::arrayValue);
    if (parsed.isObject() && parsed.isMember("upserts") && parsed["upserts"].isArray()) {
        upserts = parsed["upserts"];
    } else if (parsed.isArray()) {
        upserts = parsed;
    } else {
        if (error) *error = "ontology response was not valid JSON upserts";
        return 0;
    }

    int64_t updates = 0;
    for (const auto& upsert : upserts) {
        if (!upsert.isObject()) continue;
        if (!upsert.isMember("root_category") || !upsert["root_category"].isString()) continue;
        if (!upsert.isMember("path") || !upsert["path"].isString()) continue;
        if (!upsert.isMember("properties") || !upsert["properties"].isObject()) continue;

        auto category = ontology::OntologyStore::RootCategoryFromString(
            upsert["root_category"].asString());
        if (!category.has_value()) continue;

        auto entity = m_ontologyStore->EnsureEntityPath(
            *category, upsert["path"].asString(), "ontology intake upsert", agentId);
        if (!entity.has_value()) continue;

        const Json::Value& properties = upsert["properties"];
        for (const auto& key : properties.getMemberNames()) {
            const Json::Value& property = properties[key];
            if (!property.isObject()) continue;
            if (!property.isMember("value")) continue;

            ontology::OntologyProperty p;
            p.entity_id = entity->id;
            p.key = key;
            if (!agentId.empty()) p.agent_id = agentId;
            p.value = property["value"].isString()
                ? property["value"].asString()
                : JsonToString(property["value"]);
            if (property.isMember("value_type") && property["value_type"].isString()) {
                auto valueType = ontology::OntologyStore::PropertyTypeFromString(
                    property["value_type"].asString());
                if (valueType.has_value()) {
                    p.value_type = *valueType;
                }
            }
            if (property.isMember("linked_observation_id")
                && (property["linked_observation_id"].isInt64()
                    || property["linked_observation_id"].isUInt64()
                    || property["linked_observation_id"].isInt()
                    || property["linked_observation_id"].isUInt())) {
                p.linked_observation_id = property["linked_observation_id"].asInt64();
            } else if (property.isMember("linked_observation_id")
                       && property["linked_observation_id"].isString()) {
                // #219: numeric-string fallback — models emit IDs as strings
                // ("42") as often as numbers (42). Guarded stoll.
                try {
                    std::size_t pos = 0;
                    const std::string raw = property["linked_observation_id"].asString();
                    const long long parsed = std::stoll(raw, &pos);
                    if (pos == raw.size()) {
                        p.linked_observation_id = static_cast<int64_t>(parsed);
                    }
                } catch (...) {
                    // leave unset — handled by validation below
                }
            }
            // #219: link validation — the link must resolve to an observation
            // this run created. A hallucinated or out-of-run ID reaches
            // SetProperty and its missing-observation path deprecates the
            // property (evidence poisoning). Drop the link, keep the upsert.
            if (p.linked_observation_id.has_value()
                && validObservationIds.count(*p.linked_observation_id) == 0) {
                ALOG_WARNING("consolidation",
                             "ontology reconciler: dropping linked_observation_id "
                             << *p.linked_observation_id << " — not in this run's evidence set");
                p.linked_observation_id.reset();
            }
            // #219: sole-evidence fallback — when the run produced exactly one
            // observation and the model upserts a property without naming its
            // evidence, the property cannot be state-driven without a link.
            // Attribute it to that observation rather than shipping an
            // unlinked row.
            if (!p.linked_observation_id.has_value() && observations.size() == 1) {
                p.linked_observation_id = observations.front().id;
            }
            const std::string motivation = property.isMember("motivation")
                ? property["motivation"].asString()
                : "ontology intake upsert";
            auto updated = m_ontologyStore->SetProperty(p, motivation);
            if (updated.id > 0) updates++;
        }
    }

    m_ontologyStore->SyncPropertyStatesFromObservations(m_memoryStore);
    return updates;
}

// ============================================================================
// Layer Consolidation — evaluate, promote, demote, archive
// ============================================================================

bool ConsolidationPipeline::RunLayerConsolidation(
        const std::string& agentId,
        const std::string& layerName,
        std::string* error) {
    ConsolidationRun run;
    run.agent_id = agentId;
    run.phase = "layer_consolidation";
    run.started_unix_ms = NowMs();
    run.status = "running";
    const int64_t runId = m_store.CreateRun(run);

    int64_t promoted = 0, demoted = 0, archived = 0;

    // Find the layer (scoped to this agent)
    auto layers = m_memoryStore->ListLayersForAgent(agentId);
    memory::MemoryLayer targetLayer;
    bool found = false;
    int layerIndex = -1;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        if (layers[i].name == layerName) {
            targetLayer = layers[i];
            layerIndex = i;
            found = true;
            break;
        }
    }
    if (!found) {
        m_store.FinishRun(runId, "failed", "{}", "layer not found: " + layerName + " for agent " + agentId);
        if (error) *error = "layer not found: " + layerName + " for agent " + agentId;
        return false;
    }

    // Skip millennium layer (functionally permanent)
    if (layerName == "millennium") {
        m_store.FinishRun(runId, "completed",
                          "{\"skipped\": true, \"reason\": \"millennium layer\"}", "");
        return true;
    }

    // Get observations due for review (explicit next_review_at scheduling).
    auto observations = m_memoryStore->ListObservationsDueForReview(
        agentId, targetLayer.id, NowMs());
    if (observations.empty()) {
        m_store.FinishRun(runId, "completed",
                          "{\"observations\": 0, \"evaluation_interval_seconds\": "
                          + std::to_string(targetLayer.evaluation_interval_seconds) + "}", "");
        return true;
    }

    // Build prompt for LLM evaluation
    std::ostringstream userPrompt;
    userPrompt << "Layer: " << layerName << " (agent: " << agentId << ")\n";
    userPrompt << "Observations:\n\n";
    for (const auto& obs : observations) {
        userPrompt << "ID " << obs.id << ": " << obs.text
                   << " [weight=" << obs.weight << "]\n";
    }

    std::string llmResponse;
    try {
        std::string prompt = targetLayer.consolidation_review_prompt.empty()
            ? m_config.consolidation_prompt
            : targetLayer.consolidation_review_prompt;
        llmResponse = m_llmCallback(agentId, prompt, userPrompt.str());
    } catch (const std::exception& e) {
        m_store.FinishRun(runId, "failed", "{}",
                          std::string("LLM error: ") + e.what());
        if (error) *error = e.what();
        return false;
    }

    // Parse LLM response
    auto decisions = ParseLLMJsonArray(llmResponse, error);
    for (const auto& decision : decisions) {
        const int64_t obsId = decision.isMember("id") ? decision["id"].asInt64() : 0;
        const std::string action = decision.isMember("action")
            ? decision["action"].asString() : "retain";
        const std::string reason = decision.isMember("reason")
            ? decision["reason"].asString() : "";
        std::optional<memory::MemoryState> explicitState = std::nullopt;
        if (decision.isMember("memory_state")) {
            const Json::Value& stateNode = decision["memory_state"];
            if (stateNode.isString()) {
                const std::string stateText = stateNode.asString();
                if (stateText == "current") explicitState = memory::MemoryState::Current;
                else if (stateText == "deprecated") explicitState = memory::MemoryState::Deprecated;
                else if (stateText == "new") explicitState = memory::MemoryState::New;
            } else if (stateNode.isInt() || stateNode.isUInt()
                       || stateNode.isInt64() || stateNode.isUInt64()) {
                const auto value = stateNode.asInt64();
                if (value == 1) explicitState = memory::MemoryState::Current;
                else if (value == 2) explicitState = memory::MemoryState::Deprecated;
                else explicitState = memory::MemoryState::New;
            }
        }

        if (obsId == 0) continue;

        if (explicitState.has_value()) {
            m_memoryStore->SetObservationMemoryState(obsId, *explicitState, reason);
        } else if (action == "retain" || action == "promote" || action == "demote") {
            // Reviewed observations are current by default unless the model explicitly deprecates.
            m_memoryStore->SetObservationMemoryState(obsId, memory::MemoryState::Current, reason);
        }

        if (action == "promote" && layerIndex < static_cast<int>(layers.size()) - 1) {
            promoted += PromoteObservation(obsId, targetLayer.id, reason);
        } else if (action == "demote" && layerIndex > 0) {
            demoted += DemoteObservation(obsId, targetLayer.id, reason);
        } else if (action == "demote" && layerIndex == 0) {
            archived += ArchiveObservation(obsId, targetLayer.id, reason);
        }
        // "retain" — touch evaluation timestamp so it waits another cycle
        if (action == "retain") {
            m_memoryStore->TouchEvaluation(obsId);
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.observations_promoted += promoted;
        m_stats.observations_demoted += demoted;
        m_stats.observations_archived += archived;
        m_stats.runs_completed++;
    }

    Json::Value summary(Json::objectValue);
    summary["promoted"] = static_cast<Json::Int64>(promoted);
    summary["demoted"] = static_cast<Json::Int64>(demoted);
    summary["archived"] = static_cast<Json::Int64>(archived);

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    m_store.FinishRun(runId, "completed", Json::writeString(wb, summary), "");

    return true;
}

int ConsolidationPipeline::PromoteObservation(int64_t obsId, int64_t fromLayerId,
                                                const std::string& reason) {
    auto fromLayer = m_memoryStore->GetLayer(fromLayerId);
    if (!fromLayer) return 0;
    auto layers = m_memoryStore->ListLayersForAgent(fromLayer->agent_id);
    int fromIndex = -1;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        if (layers[i].id == fromLayerId) { fromIndex = i; break; }
    }
    if (fromIndex < 0 || fromIndex >= static_cast<int>(layers.size()) - 1) return 0;

    const int64_t toLayerId = layers[fromIndex + 1].id;
    if (m_memoryStore->MoveObservation(obsId, toLayerId, reason)) {
        memory::MemoryMutation mut;
        mut.mutation_type = "promote";
        mut.target_type = "observation";
        mut.target_id = obsId;
        mut.from_layer_id = fromLayerId;
        mut.to_layer_id = toLayerId;
        mut.motivation = reason;
        mut.unix_ms = NowMs();
        m_memoryStore->LogMutation(mut);
        return 1;
    }
    return 0;
}

int ConsolidationPipeline::DemoteObservation(int64_t obsId, int64_t fromLayerId,
                                               const std::string& reason) {
    auto fromLayer = m_memoryStore->GetLayer(fromLayerId);
    if (!fromLayer) return 0;
    auto layers = m_memoryStore->ListLayersForAgent(fromLayer->agent_id);
    int fromIndex = -1;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        if (layers[i].id == fromLayerId) { fromIndex = i; break; }
    }
    if (fromIndex <= 0) return 0;

    const int64_t toLayerId = layers[fromIndex - 1].id;
    if (m_memoryStore->MoveObservation(obsId, toLayerId, reason)) {
        memory::MemoryMutation mut;
        mut.mutation_type = "demote";
        mut.target_type = "observation";
        mut.target_id = obsId;
        mut.from_layer_id = fromLayerId;
        mut.to_layer_id = toLayerId;
        mut.motivation = reason;
        mut.unix_ms = NowMs();
        m_memoryStore->LogMutation(mut);
        return 1;
    }
    return 0;
}

int ConsolidationPipeline::ArchiveObservation(int64_t obsId, int64_t fromLayerId,
                                                const std::string& reason) {
    // Archive = delete from active layers (remains in mutation log).
    // #221: capture the full row BEFORE deleting — the mutation log
    // alone must reconstruct the archived observation.
    const auto snapshot = m_memoryStore->GetObservation(obsId);
    if (snapshot && m_memoryStore->DeleteObservation(obsId)) {
        memory::MemoryMutation mut;
        mut.mutation_type = "archive";
        mut.target_type = "observation";
        mut.target_id = obsId;
        mut.from_layer_id = fromLayerId;
        mut.row_snapshot = memory::ObservationToJson(*snapshot);
        mut.motivation = reason;
        mut.unix_ms = NowMs();
        m_memoryStore->LogMutation(mut);
        return 1;
    }
    return 0;
}

// ============================================================================
// Perspective Revision
// ============================================================================

bool ConsolidationPipeline::RunPerspectiveRevision(
        const std::string& agentId,
        const std::string& layerName,
        std::string* error) {
    ConsolidationRun run;
    run.agent_id = agentId;
    run.phase = "perspective_revision";
    run.started_unix_ms = NowMs();
    run.status = "running";
    const int64_t runId = m_store.CreateRun(run);

    // Find the layer (scoped to this agent)
    auto layers = m_memoryStore->ListLayersForAgent(agentId);
    memory::MemoryLayer targetLayer;
    bool found = false;
    for (const auto& l : layers) {
        if (l.name == layerName) { targetLayer = l; found = true; break; }
    }
    if (!found) {
        m_store.FinishRun(runId, "failed", "{}", "layer not found: " + layerName + " for agent " + agentId);
        if (error) *error = "layer not found: " + layerName;
        return false;
    }

    // Get observations for context
    auto observations = m_memoryStore->ListObservationsForLayer(targetLayer.id);

    // Skip perspective revision if there are no active (non-deprecated) observations.
    // Generating perspectives from empty or fully-retired layers causes drift —
    // the LLM invents narratives from training context rather than reflecting on data.
    int activeCount = 0;
    for (const auto& obs : observations) {
        if (obs.memory_state != memory::MemoryState::Deprecated) activeCount++;
    }
    if (activeCount == 0) {
        m_store.FinishRun(runId, "skipped", "{\"reason\": \"no active observations\"}", "");
        return true;
    }

    // Also skip if the perspective was already generated after the most recent
    // observation was created — no new data means no reason to regenerate.
    auto existing = m_memoryStore->GetPerspective(targetLayer.id);
    if (existing && existing->updated_at_unix_ms > 0) {
        int64_t newestObsTime = 0;
        for (const auto& obs : observations) {
            if (obs.memory_state != memory::MemoryState::Deprecated
                && obs.created_at_unix_ms > newestObsTime) {
                newestObsTime = obs.created_at_unix_ms;
            }
        }
        if (existing->updated_at_unix_ms >= newestObsTime) {
            m_store.FinishRun(runId, "skipped",
                "{\"reason\": \"perspective current (no new observations since last revision)\"}", "");
            return true;
        }
    }

    std::ostringstream userPrompt;
    userPrompt << "Layer: " << layerName << " (agent: " << agentId << ")\n";
    userPrompt << "Observations in this layer:\n\n";
    for (const auto& obs : observations) {
        userPrompt << "- " << obs.text << "\n";
    }
    userPrompt << "\n";

    // existing perspective already retrieved above for staleness check
    if (existing) {
        userPrompt << "Previous perspectives:\n";
        if (!existing->retrospective.empty()) {
            userPrompt << "  Retrospective: " << existing->retrospective << "\n";
        }
        if (!existing->current_perspective.empty()) {
            userPrompt << "  Current: " << existing->current_perspective << "\n";
        }
        if (!existing->future_perspective.empty()) {
            userPrompt << "  Future: " << existing->future_perspective << "\n";
        }
        userPrompt << "\n";
    }

    std::string llmResponse;
    try {
        llmResponse = m_llmCallback(agentId, m_config.perspective_prompt,
                                     userPrompt.str());
    } catch (const std::exception& e) {
        m_store.FinishRun(runId, "failed", "{}",
                          std::string("LLM error: ") + e.what());
        if (error) *error = e.what();
        return false;
    }

    // Parse response
    auto jsonStr = ExtractJsonFromResponse(llmResponse);
    auto parsed = ParseJson(jsonStr);

    if (!parsed.isObject()) {
        m_store.FinishRun(runId, "failed", "{}", "LLM response not a JSON object");
        if (error) *error = "LLM response not a JSON object";
        return false;
    }

    memory::LayerPerspective perspective;
    perspective.layer_id = targetLayer.id;
    perspective.retrospective = parsed.isMember("retrospective")
        ? parsed["retrospective"].asString() : "";
    perspective.current_perspective = parsed.isMember("current")
        ? parsed["current"].asString() : "";
    perspective.future_perspective = parsed.isMember("future")
        ? parsed["future"].asString() : "";
    perspective.updated_at_unix_ms = NowMs();

    if (!m_memoryStore->SetPerspective(perspective)) {
        m_store.FinishRun(runId, "failed", "{}",
                          "perspective write not confirmed for layer " + layerName);
        if (error) *error = "perspective write not confirmed for layer " + layerName;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.perspectives_updated++;
        m_stats.runs_completed++;
    }

    Json::Value summary(Json::objectValue);
    summary["layer"] = layerName;
    summary["updated"] = true;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    m_store.FinishRun(runId, "completed", Json::writeString(wb, summary), "");

    return true;
}

// ============================================================================
// Full pipeline — intake + consolidation + perspectives for one agent
// ============================================================================

bool ConsolidationPipeline::RunFullPipeline(const std::string& agentId,
                                              std::string* error) {
    // 1. Intake
    if (m_config.intake_enabled) {
        if (!RunIntake(agentId, std::nullopt, error)) return false;
    }

    // 2. Layer consolidation (bottom to top, skip millennium)
    if (m_config.consolidation_enabled) {
        auto layers = m_memoryStore->ListLayersForAgent(agentId);
        for (const auto& layer : layers) {
            if (layer.name == "millennium") continue;

            std::string layerErr;
            RunLayerConsolidation(agentId, layer.name, &layerErr);
        }
    }

    // 3. Perspective revision
    if (m_config.perspectives_enabled &&
        m_config.perspectives_update_after_consolidation) {
        auto layers = m_memoryStore->ListLayersForAgent(agentId);
        for (const auto& layer : layers) {
            if (layer.name == "millennium") continue;

            std::string perspErr;
            RunPerspectiveRevision(agentId, layer.name, &perspErr);
        }
    }

    return true;
}

// ============================================================================
// Stats + status
// ============================================================================

ConsolidationPipeline::PipelineStats ConsolidationPipeline::GetStats() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_stats;
}

Json::Value ConsolidationPipeline::GetStatusJson() const {
    auto stats = GetStats();
    Json::Value out(Json::objectValue);
    out["running"] = m_running.load();
    out["observations_created"] = static_cast<Json::Int64>(stats.observations_created);
    out["observations_promoted"] = static_cast<Json::Int64>(stats.observations_promoted);
    out["observations_demoted"] = static_cast<Json::Int64>(stats.observations_demoted);
    out["observations_archived"] = static_cast<Json::Int64>(stats.observations_archived);
    out["perspectives_updated"] = static_cast<Json::Int64>(stats.perspectives_updated);
    out["runs_completed"] = static_cast<Json::Int64>(stats.runs_completed);
    out["runs_failed"] = static_cast<Json::Int64>(stats.runs_failed);
    return out;
}

// ============================================================================
// LLM response parsing
// ============================================================================

std::vector<Json::Value> ConsolidationPipeline::ParseLLMJsonArray(
        const std::string& llmResponse, std::string* error) {
    std::vector<Json::Value> result;

    auto jsonStr = ExtractJsonFromResponse(llmResponse);
    auto parsed = ParseJson(jsonStr);

    if (parsed.isArray()) {
        for (const auto& item : parsed) {
            result.push_back(item);
        }
    } else if (parsed.isObject()) {
        // Single object — wrap in array
        result.push_back(parsed);
    } else {
        if (error) *error = "LLM response was not valid JSON";
    }

    return result;
}

} // namespace animus::kernel
