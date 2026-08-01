<script setup lang="ts">
import { ref, onMounted, computed, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { apiGet, apiRequest } from '../lib/api';

const { t } = useI18n();

// ── Types ──────────────────────────────────────────────────────────────────

interface SessionReport {
  id: number;
  session_id: number;
  agent_id?: string;
  summary: string;
  past_events?: string;
  current_activity?: string;
  forward_look?: string;
  created_at_unix_ms?: number;
  updated_at_unix_ms: number;
}

interface FullReport extends SessionReport {
  agent_id: string;
  past_events: string;
  current_activity: string;
  forward_look: string;
  created_at_unix_ms: number;
}

interface AgentSummary {
  id: string;
  name: string;
}

interface ScheduledTask {
  id: string;
  agent_id: string;
  tag: string;
  type: string;
  cron_expr: string;
  timezone: string;
  message: string;
  enabled: boolean;
  next_fire: string;
  last_fire: string;
  fire_count: number;
  created_at: string;
}

// ── State ──────────────────────────────────────────────────────────────────

const reports = ref<SessionReport[]>([]);
const loading = ref(true);
const error = ref('');
const searchQuery = ref('');
const searchResults = ref<SessionReport[] | null>(null);
const selectedReport = ref<FullReport | null>(null);
const detailLoading = ref(false);
const selectedAgent = ref('');
const agents = ref<AgentSummary[]>([]);

// Scheduler state
const scheduledTask = ref<ScheduledTask | null>(null);
const taskLoading = ref(false);
const taskError = ref('');
const showTaskPanel = ref(true);
const intervalPreset = ref('1h');
const customCron = ref('0 * * * *');
const showCustomCron = ref(false);
const creatingTask = ref(false);
const removingTask = ref(false);
const confirmRemove = ref(false);
const triggeringReport = ref(false);
const triggerReportError = ref('');

const intervalPresets = [
  { value: '30m', title: 'Every 30 minutes', cron: '*/30 * * * *' },
  { value: '1h', title: 'Every hour', cron: '0 * * * *' },
  { value: '3h', title: 'Every 3 hours', cron: '0 */3 * * *' },
  { value: '6h', title: 'Every 6 hours', cron: '0 */6 * * *' },
  { value: '12h', title: 'Every 12 hours', cron: '0 */12 * * *' },
  { value: '24h', title: 'Every 24 hours', cron: '0 0 * * *' },
  { value: 'custom', title: 'Custom cron expression', cron: '' },
];

const selectedCron = computed(() => {
  if (intervalPreset.value === 'custom') return customCron.value;
  const preset = intervalPresets.find(p => p.value === intervalPreset.value);
  return preset ? preset.cron : '0 * * * *';
});

const agentItems = computed(() => {
  return agents.value.map((a) => ({
    value: a.id,
    title: `${a.name} (${a.id})`,
  }));
});

// ── Computed ───────────────────────────────────────────────────────────────

const displayedReports = computed(() => {
  if (searchResults.value !== null) return searchResults.value;
  return reports.value;
});

// ── Actions ────────────────────────────────────────────────────────────────

async function loadAgents() {
  try {
    const data = await apiGet<{ agents: AgentSummary[] }>('/api/v1/agents');
    agents.value = data.agents ?? [];
    if (agents.value.length > 0 && !selectedAgent.value) {
      selectedAgent.value = agents.value[0].id;
    }
  } catch {
    agents.value = [];
  }
}

async function fetchReports() {
  if (!selectedAgent.value) {
    reports.value = [];
    loading.value = false;
    return;
  }
  loading.value = true;
  error.value = '';
  try {
    const res = await apiGet<{ reports: SessionReport[]; count: number }>(
      `/api/v1/agents/${selectedAgent.value}/session-reports?limit=50`,
    );
    reports.value = res.reports ?? [];
  } catch (e: unknown) {
    error.value = e instanceof Error ? e.message : 'Failed to load reports';
  } finally {
    loading.value = false;
  }
}

// ── Scheduler actions ─────────────────────────────────────────────────────

async function fetchScheduledTask() {
  if (!selectedAgent.value) {
    scheduledTask.value = null;
    return;
  }
  taskLoading.value = true;
  taskError.value = '';
  try {
    const res = await apiGet<{ tasks: ScheduledTask[]; count: number }>(
      `/api/v1/agents/${selectedAgent.value}/scheduled-tasks`,
    );
    const recurring = (res.tasks ?? []).find(t => t.type === 'recurring' && t.enabled);
    scheduledTask.value = recurring ?? null;
  } catch (e: unknown) {
    taskError.value = e instanceof Error ? e.message : 'Failed to load scheduled task';
  } finally {
    taskLoading.value = false;
  }
}

async function createScheduledTask() {
  if (!selectedAgent.value) return;
  creatingTask.value = true;
  taskError.value = '';
  try {
    await apiRequest('POST', `/api/v1/agents/${selectedAgent.value}/scheduled-tasks`, {
      cron_expr: selectedCron.value,
      message: 'intake:day',
    });
    await fetchScheduledTask();
  } catch (e: unknown) {
    taskError.value = e instanceof Error ? e.message : 'Failed to create task';
  } finally {
    creatingTask.value = false;
  }
}

async function removeScheduledTask() {
  if (!scheduledTask.value) return;
  removingTask.value = true;
  taskError.value = '';
  try {
    await apiRequest('DELETE', `/api/v1/scheduled-tasks/${scheduledTask.value.id}`);
    scheduledTask.value = null;
    confirmRemove.value = false;
  } catch (e: unknown) {
    taskError.value = e instanceof Error ? e.message : 'Failed to remove task';
  } finally {
    removingTask.value = false;
  }
}

function formatIsoDate(iso: string): string {
  if (!iso) return '—';
  try {
    return new Date(iso).toLocaleString(undefined, {
      month: 'short', day: 'numeric',
      hour: '2-digit', minute: '2-digit',
    });
  } catch {
    return iso;
  }
}

function cronToHuman(cron: string): string {
  const match = intervalPresets.find(p => p.cron === cron);
  if (match) return match.title;
  return cron;
}

async function triggerSessionReport() {
  if (!selectedAgent.value) return;
  triggeringReport.value = true;
  triggerReportError.value = '';
  try {
    await apiRequest('POST', `/api/v1/memory/agents/${selectedAgent.value}/session-report`);
  } catch (e: unknown) {
    triggerReportError.value = e instanceof Error ? e.message : 'Failed to trigger session report';
  } finally {
    triggeringReport.value = false;
  }
}

async function openReport(report: SessionReport) {
  detailLoading.value = true;
  selectedReport.value = null;
  error.value = '';
  try {
    const res = await apiGet<FullReport>(
      `/api/v1/sessions/${report.session_id}/report?agent_id=${report.agent_id || selectedAgent.value}`,
    );
    selectedReport.value = res;
  } catch (e: unknown) {
    error.value = e instanceof Error ? e.message : 'Failed to load report';
  } finally {
    detailLoading.value = false;
  }
}

async function saveReport() {
  if (!selectedReport.value) return;
  error.value = '';
  try {
    const r = selectedReport.value;
    await apiRequest('PUT', `/api/v1/sessions/${r.session_id}/report`, {
      agent_id: r.agent_id,
      summary: r.summary,
      past_events: r.past_events,
      current_activity: r.current_activity,
      forward_look: r.forward_look,
    });
    // Update in list
    const idx = reports.value.findIndex(
      x => x.session_id === r.session_id && x.agent_id === r.agent_id,
    );
    if (idx >= 0) {
      reports.value[idx] = {
        ...reports.value[idx],
        summary: r.summary,
        current_activity: r.current_activity,
        forward_look: r.forward_look,
        updated_at_unix_ms: Date.now(),
      };
    }
    selectedReport.value = null;
  } catch (e: unknown) {
    error.value = e instanceof Error ? e.message : 'Failed to save report';
  }
}

async function deleteReport(report: SessionReport) {
  error.value = '';
  try {
    await apiRequest(
      'DELETE',
      `/api/v1/sessions/${report.session_id}/report?agent_id=${report.agent_id || selectedAgent.value}`,
    );
    reports.value = reports.value.filter(
      r => !(r.session_id === report.session_id && r.agent_id === report.agent_id),
    );
    if (selectedReport.value?.session_id === report.session_id) {
      selectedReport.value = null;
    }
  } catch (e: unknown) {
    error.value = e instanceof Error ? e.message : 'Failed to delete report';
  }
}

async function runSearch() {
  if (!searchQuery.value.trim()) {
    searchResults.value = null;
    return;
  }
  loading.value = true;
  error.value = '';
  try {
    const res = await apiGet<{ reports: SessionReport[]; count: number }>(
      `/api/v1/agents/${selectedAgent.value}/session-reports/search?q=${encodeURIComponent(searchQuery.value)}`,
    );
    searchResults.value = res.reports ?? [];
  } catch (e: unknown) {
    error.value = e instanceof Error ? e.message : 'Search failed';
  } finally {
    loading.value = false;
  }
}

function clearSearch() {
  searchQuery.value = '';
  searchResults.value = null;
}

function formatDate(unixMs: number): string {
  if (!unixMs) return '—';
  return new Date(unixMs).toLocaleDateString(undefined, {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  });
}

function relativeTime(unixMs: number): string {
  if (!unixMs) return '';
  const diff = Date.now() - unixMs;
  const mins = Math.floor(diff / 60000);
  if (mins < 1) return 'just now';
  if (mins < 60) return `${mins}m ago`;
  const hours = Math.floor(mins / 60);
  if (hours < 24) return `${hours}h ago`;
  const days = Math.floor(hours / 24);
  return `${days}d ago`;
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

onMounted(async () => {
  await loadAgents();
  await fetchReports();
  await fetchScheduledTask();
});

// Reload task when agent changes
watch(selectedAgent, () => {
  fetchScheduledTask();
});
</script>

<template>
  <div>
    <div class="d-flex align-center mb-4">
      <h1 class="text-h5 mr-4">Session Reports</h1>
      <v-chip size="small" color="primary" variant="tonal">
        {{ displayedReports.length }}
      </v-chip>
      <v-spacer />
      <v-btn
        variant="text"
        size="small"
        prepend-icon="mdi-refresh"
        @click="fetchReports"
      >
        Refresh
      </v-btn>
    </div>

    <v-alert v-if="error" type="error" variant="tonal" closable class="mb-4" @click:close="error = ''" />

    <!-- Scheduling panel -->
    <div class="d-flex align-center gap-2 mb-4">
      <v-expansion-panels v-model="showTaskPanel" class="flex-grow-1">
        <v-expansion-panel>
          <v-expansion-panel-title>
            <div class="d-flex align-center gap-2">
              <v-icon size="18" :color="scheduledTask ? 'success' : 'grey-lighten-1'">
                {{ scheduledTask ? 'mdi-clock-check-outline' : 'mdi-clock-outline' }}
              </v-icon>
              <span class="text-subtitle-2">Consolidation Schedule</span>
              <v-chip v-if="scheduledTask" size="x-small" color="success" variant="tonal">
                Active
              </v-chip>
              <v-chip v-else size="x-small" color="grey" variant="tonal">
                Not scheduled
              </v-chip>
            </div>
          </v-expansion-panel-title>
        <v-expansion-panel-text>
          <!-- Loading state -->
          <div v-if="taskLoading" class="pa-4 text-center">
            <v-progress-circular indeterminate size="24" />
          </div>

          <!-- No task: creation form -->
          <div v-else-if="!scheduledTask">
            <v-alert v-if="taskError" type="error" variant="tonal" density="compact" class="mb-3">
              {{ taskError }}
            </v-alert>
            <div class="d-flex align-center gap-3 flex-wrap">
              <v-select
                v-model="intervalPreset"
                :items="intervalPresets.map(p => ({ value: p.value, title: p.title }))"
                density="compact"
                variant="outlined"
                label="Interval"
                hide-details
                style="max-width: 220px;"
                @update:model-value="(v: string) => showCustomCron = v === 'custom'"
              />
              <v-text-field
                v-if="showCustomCron"
                v-model="customCron"
                density="compact"
                variant="outlined"
                label="Cron expression"
                placeholder="0 * * * *"
                hint="minute hour day month weekday"
                persistent-hint
                hide-details
                style="max-width: 200px;"
              />
              <v-btn
                color="primary"
                variant="tonal"
                :loading="creatingTask"
                @click="createScheduledTask"
              >
                <v-icon start>mdi-plus</v-icon>
                Create Scheduled Task
              </v-btn>
            </div>
          </div>

          <!-- Task exists: info + remove -->
          <div v-else>
            <v-alert v-if="taskError" type="error" variant="tonal" density="compact" class="mb-3">
              {{ taskError }}
            </v-alert>
            <div class="d-flex align-start gap-3 flex-wrap mb-3">
              <div class="flex-grow-1">
                <div class="text-body-2 mb-1">
                  <span class="text-medium-emphasis">Schedule ID:</span>
                  <code class="ml-2 text-caption">{{ scheduledTask.id.substring(0, 12) }}…</code>
                </div>
                <div class="text-body-2 mb-1">
                  <span class="text-medium-emphasis">Interval:</span>
                  <span class="ml-2">{{ cronToHuman(scheduledTask.cron_expr) }}</span>
                </div>
                <div class="text-body-2 mb-1">
                  <span class="text-medium-emphasis">Next fire:</span>
                  <span class="ml-2">{{ formatIsoDate(scheduledTask.next_fire) }}</span>
                </div>
                <div class="text-body-2 mb-1">
                  <span class="text-medium-emphasis">Last fire:</span>
                  <span class="ml-2">{{ formatIsoDate(scheduledTask.last_fire) }}</span>
                </div>
                <div class="text-body-2">
                  <span class="text-medium-emphasis">Fire count:</span>
                  <span class="ml-2">{{ scheduledTask.fire_count }}</span>
                </div>
              </div>
              <div v-if="!confirmRemove">
                <v-btn
                  color="error"
                  variant="text"
                  size="small"
                  :loading="removingTask"
                  @click="confirmRemove = true"
                >
                  <v-icon start>mdi-delete-outline</v-icon>
                  Remove
                </v-btn>
              </div>
              <div v-else class="d-flex gap-2">
                <v-btn color="error" variant="tonal" size="small" :loading="removingTask" @click="removeScheduledTask">
                  Confirm
                </v-btn>
                <v-btn variant="text" size="small" @click="confirmRemove = false">
                  Cancel
                </v-btn>
              </div>
            </div>
          </div>
        </v-expansion-panel-text>
      </v-expansion-panel>
    </v-expansion-panels>
      <v-btn
        color="primary"
        variant="tonal"
        size="small"
        :loading="triggeringReport"
        prepend-icon="mdi-flash-outline"
        @click="triggerSessionReport"
      >
        Run Session Report
      </v-btn>
    </div>

    <v-alert v-if="triggerReportError" type="error" variant="tonal" density="compact" class="mb-2" closable @click:close="triggerReportError = ''" />

    <!-- Search bar -->
    <div class="d-flex align-center gap-2 mb-4">
      <v-text-field
        v-model="searchQuery"
        density="compact"
        variant="outlined"
        placeholder="Search reports..."
        prepend-inner-icon="mdi-magnify"
        hide-details
        clearable
        @keyup.enter="runSearch"
        @click:clear="clearSearch"
        style="max-width: 400px;"
      />
      <v-select
        v-model="selectedAgent"
        density="compact"
        variant="outlined"
        :items="agentItems"
        hide-details
        label="Agent"
        style="max-width: 200px;"
        @update:model-value="fetchReports"
      />
      <v-btn
        v-if="searchResults !== null"
        variant="text"
        size="small"
        @click="clearSearch"
      >
        Clear search
      </v-btn>
    </div>

    <!-- Two-column layout: list + detail -->
    <div class="d-flex gap-4" style="align-items: flex-start;">
      <!-- Left: Report timeline -->
      <div style="flex: 1; min-width: 0;">
        <v-progress-linear v-if="loading" indeterminate color="primary" class="mb-2" />

        <div v-if="!loading && displayedReports.length === 0" class="pa-8 text-center text-medium-emphasis">
          <v-icon size="48" class="mb-2" color="grey-lighten-1">mdi-file-document-outline</v-icon>
          <div class="text-body-1">No session reports</div>
          <div class="text-body-2 mt-1">
            Reports are created during consolidation intake.
          </div>
        </div>

        <!-- Timeline list -->
        <div v-if="displayedReports.length > 0" class="report-timeline">
          <div
            v-for="(report, i) in displayedReports"
            :key="report.id"
            class="report-item"
            :class="{ active: selectedReport?.session_id === report.session_id }"
          >
            <!-- Timeline dot + line -->
            <div class="timeline-marker">
              <div class="timeline-dot" />
              <div v-if="i < displayedReports.length - 1" class="timeline-line" />
            </div>

            <!-- Content -->
            <div
              class="timeline-content"
              @click="openReport(report)"
            >
              <div class="d-flex align-center gap-2 mb-1">
                <v-chip size="x-small" variant="flat" color="primary" class="font-mono">
                  #{{ report.session_id }}
                </v-chip>
                <span class="text-caption text-medium-emphasis">
                  {{ relativeTime(report.updated_at_unix_ms) }}
                </span>
              </div>
              <div class="text-body-2 summary-text">
                {{ report.summary || '(no summary)' }}
              </div>
              <div v-if="report.current_activity" class="text-caption text-medium-emphasis mt-1">
                <v-icon size="12" class="mr-1">mdi-circle-medium</v-icon>
                {{ report.current_activity }}
              </div>
              <div v-if="report.forward_look" class="text-caption text-primary mt-1" style="opacity: 0.8;">
                <v-icon size="12" class="mr-1">mdi-arrow-right</v-icon>
                {{ report.forward_look }}
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Right: Detail panel -->
      <div style="flex: 1; min-width: 0; max-width: 480px;">
        <v-card v-if="!selectedReport && !detailLoading" variant="outlined" class="pa-8 text-center text-medium-emphasis">
          <v-icon size="48" class="mb-2" color="grey-lighten-1">mdi-clock-outline</v-icon>
          <div class="text-body-2">Select a report to view details</div>
        </v-card>

        <v-progress-linear v-if="detailLoading" indeterminate color="primary" />

        <v-card v-if="selectedReport" variant="outlined">
          <v-card-title class="text-subtitle-1 d-flex align-center">
            <v-chip size="small" variant="flat" color="primary" class="font-mono mr-2">
              #{{ selectedReport.session_id }}
            </v-chip>
            <span class="text-caption text-medium-emphasis">
              {{ formatDate(selectedReport.updated_at_unix_ms) }}
            </span>
            <v-spacer />
            <v-btn
              icon="mdi-delete-outline"
              variant="text"
              size="x-small"
              color="error"
              @click="deleteReport(selectedReport)"
            />
          </v-card-title>
          <v-divider />

          <v-card-text>
            <!-- Summary -->
            <div class="mb-4">
              <div class="text-overline text-primary mb-1">Summary</div>
              <div class="text-body-2">{{ selectedReport.summary || '(empty)' }}</div>
            </div>

            <!-- Past Events -->
            <div class="mb-4">
              <div class="text-overline text-secondary mb-1">
                <v-icon size="14" class="mr-1">mdi-history</v-icon>
                Past Events
              </div>
              <div class="text-body-2">{{ selectedReport.past_events || '(empty)' }}</div>
            </div>

            <!-- Current Activity -->
            <div class="mb-4">
              <div class="text-overline text-success mb-1">
                <v-icon size="14" class="mr-1">mdi-circle-medium</v-icon>
                Current Activity
              </div>
              <div class="text-body-2">{{ selectedReport.current_activity || '(empty)' }}</div>
            </div>

            <!-- Forward Look -->
            <div class="mb-2">
              <div class="text-overline text-info mb-1">
                <v-icon size="14" class="mr-1">mdi-arrow-right</v-icon>
                Forward Look
              </div>
              <div class="text-body-2">{{ selectedReport.forward_look || '(empty)' }}</div>
            </div>
          </v-card-text>

          <v-divider />
          <v-card-actions>
            <v-spacer />
            <v-btn variant="text" @click="selectedReport = null">Close</v-btn>
          </v-card-actions>
        </v-card>
      </div>
    </div>
  </div>
</template>

<style scoped>
.gap-2 {
  gap: 8px;
}

.report-timeline {
  position: relative;
}

.report-item {
  display: flex;
  gap: 16px;
  padding-bottom: 4px;
}

.timeline-marker {
  display: flex;
  flex-direction: column;
  align-items: center;
  width: 12px;
  padding-top: 6px;
}

.timeline-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: rgb(var(--v-theme-primary));
  opacity: 0.6;
  flex-shrink: 0;
  z-index: 1;
}

.report-item.active .timeline-dot {
  opacity: 1;
  box-shadow: 0 0 0 4px rgba(var(--v-theme-primary), 0.15);
}

.timeline-line {
  width: 2px;
  flex: 1;
  background: rgba(var(--v-theme-on-surface), 0.1);
  margin-top: 2px;
  min-height: 24px;
}

.timeline-content {
  flex: 1;
  min-width: 0;
  padding: 8px 12px 16px;
  border-radius: 8px;
  cursor: pointer;
  transition: background 0.15s;
}

.timeline-content:hover {
  background: rgba(var(--v-theme-on-surface), 0.04);
}

.report-item.active .timeline-content {
  background: rgba(var(--v-theme-primary), 0.06);
}

.summary-text {
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
}
</style>
