<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { apiGet } from '../lib/api';

// ── Types ──────────────────────────────────────────────────────────────────

interface PromptLogEntry {
  id: number;
  agent_id: string;
  session_id?: number;
  provider: string;
  model: string;
  prompt_tokens: number;
  completion_tokens: number;
  total_tokens: number;
  latency_ms: number;
  chain_step: number;
  created_at: string;
  prompt_content?: string;
  response_content?: string;
  tool_calls?: Array<{ id: string; name: string; arguments: string }>;
}

interface UsageSummary {
  total_calls: number;
  total_prompt_tokens: number;
  total_completion_tokens: number;
  total_tokens: number;
  avg_latency_ms: number;
}

// ── State ──────────────────────────────────────────────────────────────────

const logs = ref<PromptLogEntry[]>([]);
const summary = ref<UsageSummary | null>(null);
const loading = ref(true);
const error = ref('');
const selectedAgent = ref('default');
const selectedLog = ref<PromptLogEntry | null>(null);
const timeRange = ref<'24h' | '7d' | '30d' | 'all'>('24h');

// ── Computed ───────────────────────────────────────────────────────────────

const displayedLogs = computed(() => logs.value);

const totalTokens = computed(() =>
  logs.value.reduce((sum, l) => sum + l.total_tokens, 0)
);

const avgLatency = computed(() => {
  if (logs.value.length === 0) return 0;
  return Math.round(logs.value.reduce((sum, l) => sum + l.latency_ms, 0) / logs.value.length);
});

const callsCount = computed(() => logs.value.length);

// Bar chart data: tokens per call
const chartData = computed(() => {
  const items = [...logs.value].reverse().slice(-40); // last 40 calls
  return items.map(l => ({
    prompt: l.prompt_tokens,
    completion: l.completion_tokens,
    label: l.model.split('/').pop() || l.model,
  }));
});

const maxStacked = computed(() => {
  return Math.max(...chartData.value.map(d => d.prompt + d.completion), 1);
});

// ── Helpers ────────────────────────────────────────────────────────────────

function isoFromRange(range: string): string {
  if (range === 'all') return '';
  const now = new Date();
  const days = range === '24h' ? 1 : range === '7d' ? 7 : 30;
  now.setDate(now.getDate() - days);
  return now.toISOString();
}

function formatTime(iso: string): string {
  if (!iso) return '—';
  return new Date(iso).toLocaleTimeString(undefined, {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  });
}

function formatNumber(n: number): string {
  if (n >= 1_000_000) return (n / 1_000_000).toFixed(1) + 'M';
  if (n >= 1_000) return (n / 1_000).toFixed(1) + 'k';
  return n.toString();
}

function modelShort(model: string): string {
  return model.split('/').pop() || model;
}

function providerColor(provider: string): string {
  const colors: Record<string, string> = {
    openai: 'green',
    deepseek: 'blue',
    ollama: 'orange',
    cohere: 'purple',
    mistral: 'red',
    openrouter: 'teal',
    zai: 'indigo',
    alibaba: 'amber',
  };
  return colors[provider.toLowerCase()] || 'grey';
}

// ── Actions ────────────────────────────────────────────────────────────────

async function fetchData() {
  loading.value = true;
  error.value = '';
  try {
    const fromIso = isoFromRange(timeRange.value);
    const fromParam = fromIso ? `&from=${encodeURIComponent(fromIso)}` : '';

    const [logsRes, summaryRes] = await Promise.all([
      apiGet<{ logs: PromptLogEntry[]; count: number }>(
        `/api/v1/agents/${selectedAgent.value}/prompt-logs?limit=200${fromParam}`,
      ),
      apiGet<UsageSummary>(
        `/api/v1/agents/${selectedAgent.value}/prompt-logs/summary${fromIso ? `?from=${encodeURIComponent(fromIso)}` : ''}`,
      ),
    ]);

    logs.value = logsRes.logs ?? [];
    summary.value = summaryRes;
  } catch (e: unknown) {
    error.value = e instanceof Error ? e.message : 'Failed to load prompt logs';
    logs.value = [];
    summary.value = null;
  } finally {
    loading.value = false;
  }
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

onMounted(() => {
  fetchData();
});
</script>

<template>
  <div>
    <div class="d-flex align-center mb-4">
      <h1 class="text-h5 mr-4">Prompt Logs</h1>
      <v-chip size="small" color="primary" variant="tonal">{{ callsCount }}</v-chip>
      <v-spacer />

      <!-- Time range selector -->
      <v-btn-toggle v-model="timeRange" mandatory density="compact" variant="outlined" class="mr-2" @update:model-value="fetchData">
        <v-btn value="24h" size="small">24h</v-btn>
        <v-btn value="7d" size="small">7d</v-btn>
        <v-btn value="30d" size="small">30d</v-btn>
        <v-btn value="all" size="small">All</v-btn>
      </v-btn-toggle>

      <v-btn variant="text" size="small" prepend-icon="mdi-refresh" @click="fetchData">
        Refresh
      </v-btn>
    </div>

    <v-alert v-if="error" type="error" variant="tonal" closable class="mb-4" @click:close="error = ''" />

    <!-- Summary cards -->
    <div v-if="summary" class="d-flex gap-3 mb-4 flex-wrap">
      <v-card variant="outlined" class="stat-card flex-grow-0">
        <v-card-text class="text-center">
          <div class="text-caption text-medium-emphasis mb-1">Total Calls</div>
          <div class="text-h6">{{ formatNumber(summary.total_calls) }}</div>
        </v-card-text>
      </v-card>
      <v-card variant="outlined" class="stat-card flex-grow-0">
        <v-card-text class="text-center">
          <div class="text-caption text-medium-emphasis mb-1">Prompt Tokens</div>
          <div class="text-h6 text-info">{{ formatNumber(summary.total_prompt_tokens) }}</div>
        </v-card-text>
      </v-card>
      <v-card variant="outlined" class="stat-card flex-grow-0">
        <v-card-text class="text-center">
          <div class="text-caption text-medium-emphasis mb-1">Completion Tokens</div>
          <div class="text-h6 text-success">{{ formatNumber(summary.total_completion_tokens) }}</div>
        </v-card-text>
      </v-card>
      <v-card variant="outlined" class="stat-card flex-grow-0">
        <v-card-text class="text-center">
          <div class="text-caption text-medium-emphasis mb-1">Total Tokens</div>
          <div class="text-h6 text-primary">{{ formatNumber(summary.total_tokens) }}</div>
        </v-card-text>
      </v-card>
      <v-card variant="outlined" class="stat-card flex-grow-0">
        <v-card-text class="text-center">
          <div class="text-caption text-medium-emphasis mb-1">Avg Latency</div>
          <div class="text-h6">{{ summary.avg_latency_ms > 0 ? Math.round(summary.avg_latency_ms) + 'ms' : '—' }}</div>
        </v-card-text>
      </v-card>
    </div>

    <div class="d-flex gap-4" style="align-items: flex-start;">
      <!-- Left: Chart + Log list -->
      <div style="flex: 1; min-width: 0;">
        <v-progress-linear v-if="loading" indeterminate color="primary" class="mb-2" />

        <!-- Token usage bar chart -->
        <v-card v-if="chartData.length > 0" variant="outlined" class="mb-4">
          <v-card-text>
            <div class="text-overline text-primary mb-2">Token Usage (last {{ chartData.length }} calls)</div>
            <div class="chart-container">
              <div v-for="(d, i) in chartData" :key="i" class="chart-bar-group">
                <div class="chart-bar-stack">
                  <div
                    class="chart-bar chart-bar-prompt"
                    :style="{ height: (d.prompt / maxStacked * 100) + '%' }"
                    :title="`Prompt: ${d.prompt}`"
                  />
                  <div
                    class="chart-bar chart-bar-completion"
                    :style="{ height: (d.completion / maxStacked * 100) + '%' }"
                    :title="`Completion: ${d.completion}`"
                  />
                </div>
              </div>
            </div>
            <div class="d-flex gap-4 mt-2">
              <div class="d-flex align-center gap-1">
                <div class="chart-legend chart-legend-prompt" />
                <span class="text-caption">Prompt</span>
              </div>
              <div class="d-flex align-center gap-1">
                <div class="chart-legend chart-legend-completion" />
                <span class="text-caption">Completion</span>
              </div>
            </div>
          </v-card-text>
        </v-card>

        <div v-if="!loading && displayedLogs.length === 0" class="pa-8 text-center text-medium-emphasis">
          <v-icon size="48" class="mb-2" color="grey-lighten-1">mdi-chart-line-variant</v-icon>
          <div class="text-body-1">No prompt logs</div>
          <div class="text-body-2 mt-1">
            Logs are created when the kernel processes LLM calls.
          </div>
        </div>

        <!-- Log entries -->
        <div v-if="displayedLogs.length > 0" class="log-list">
          <div
            v-for="entry in displayedLogs"
            :key="entry.id"
            class="log-row"
            :class="{ active: selectedLog?.id === entry.id }"
            @click="selectedLog = selectedLog?.id === entry.id ? null : entry"
          >
            <div class="d-flex align-center gap-2">
              <v-chip size="x-small" :color="providerColor(entry.provider)" variant="tonal">
                {{ entry.provider }}
              </v-chip>
              <span class="text-caption font-mono">{{ modelShort(entry.model) }}</span>
              <span class="text-caption text-medium-emphasis">{{ formatTime(entry.created_at) }}</span>
              <v-spacer />
              <span class="text-caption text-medium-emphasis">#{{ entry.chain_step }}</span>
            </div>
            <div class="d-flex gap-3 mt-1">
              <span class="text-body-2">
                <v-icon size="12" color="info">mdi-arrow-down</v-icon>
                {{ formatNumber(entry.prompt_tokens) }}
              </span>
              <span class="text-body-2">
                <v-icon size="12" color="success">mdi-arrow-up</v-icon>
                {{ formatNumber(entry.completion_tokens) }}
              </span>
              <span class="text-body-2 text-medium-emphasis">
                <v-icon size="12">mdi-timer-outline</v-icon>
                {{ entry.latency_ms }}ms
              </span>
            </div>
          </div>
        </div>
      </div>

      <!-- Right: Detail panel -->
      <div style="flex: 0 0 400px; max-width: 400px;">
        <v-card v-if="!selectedLog" variant="outlined" class="pa-8 text-center text-medium-emphasis">
          <v-icon size="48" class="mb-2" color="grey-lighten-1">mdi-chart-line</v-icon>
          <div class="text-body-2">Select a log entry to view details</div>
        </v-card>

        <v-card v-if="selectedLog" variant="outlined">
          <v-card-title class="text-subtitle-2 d-flex align-center">
            <v-chip size="small" :color="providerColor(selectedLog.provider)" variant="tonal" class="mr-2">
              {{ selectedLog.provider }}
            </v-chip>
            <span class="font-mono text-caption">{{ selectedLog.model }}</span>
            <v-spacer />
            <v-btn icon="mdi-close" variant="text" size="x-small" @click="selectedLog = null" />
          </v-card-title>
          <v-divider />

          <v-card-text>
            <div class="d-flex gap-3 mb-3">
              <div>
                <div class="text-caption text-medium-emphasis">Prompt</div>
                <div class="text-h6 text-info">{{ formatNumber(selectedLog.prompt_tokens) }}</div>
              </div>
              <div>
                <div class="text-caption text-medium-emphasis">Completion</div>
                <div class="text-h6 text-success">{{ formatNumber(selectedLog.completion_tokens) }}</div>
              </div>
              <div>
                <div class="text-caption text-medium-emphasis">Total</div>
                <div class="text-h6 text-primary">{{ formatNumber(selectedLog.total_tokens) }}</div>
              </div>
              <div>
                <div class="text-caption text-medium-emphasis">Latency</div>
                <div class="text-h6">{{ selectedLog.latency_ms }}ms</div>
              </div>
            </div>

            <div class="text-caption text-medium-emphasis mb-1">
              Session: {{ selectedLog.session_id ?? '—' }} · Step: {{ selectedLog.chain_step }}
            </div>
            <div class="text-caption text-medium-emphasis mb-3">
              {{ selectedLog.created_at }}
            </div>

            <div v-if="selectedLog.tool_calls && selectedLog.tool_calls.length > 0" class="mb-3">
              <div class="text-overline text-warning mb-1">Tool Calls</div>
              <div v-for="tc in selectedLog.tool_calls" :key="tc.id" class="mb-1">
                <v-chip size="x-small" variant="flat" color="warning" class="mr-1">{{ tc.name }}</v-chip>
                <span class="text-caption text-medium-emphasis">{{ tc.arguments.substring(0, 80) }}{{ tc.arguments.length > 80 ? '...' : '' }}</span>
              </div>
            </div>

            <div v-if="selectedLog.prompt_content" class="mb-3">
              <div class="text-overline text-info mb-1">Prompt</div>
              <pre class="content-pre">{{ selectedLog.prompt_content.substring(0, 500) }}{{ selectedLog.prompt_content.length > 500 ? '...' : '' }}</pre>
            </div>

            <div v-if="selectedLog.response_content">
              <div class="text-overline text-success mb-1">Response</div>
              <pre class="content-pre">{{ selectedLog.response_content.substring(0, 500) }}{{ selectedLog.response_content.length > 500 ? '...' : '' }}</pre>
            </div>
          </v-card-text>
        </v-card>
      </div>
    </div>
  </div>
</template>

<style scoped>
.gap-1 { gap: 4px; }
.gap-2 { gap: 8px; }
.gap-3 { gap: 12px; }

.stat-card {
  min-width: 120px;
}

.font-mono {
  font-family: monospace;
}

/* Chart */
.chart-container {
  display: flex;
  align-items: flex-end;
  gap: 2px;
  height: 120px;
  overflow-x: auto;
  padding-bottom: 4px;
}

.chart-bar-group {
  display: flex;
  flex-direction: column;
  justify-content: flex-end;
  min-width: 12px;
  flex: 1;
  height: 100%;
}

.chart-bar-stack {
  display: flex;
  flex-direction: column;
  justify-content: flex-end;
  height: 100%;
  min-height: 2px;
}

.chart-bar {
  min-height: 1px;
  border-radius: 2px 2px 0 0;
  transition: height 0.2s;
}

.chart-bar-prompt {
  background: rgb(var(--v-theme-info));
  opacity: 0.7;
}

.chart-bar-completion {
  background: rgb(var(--v-theme-success));
  opacity: 0.8;
}

.chart-legend {
  width: 12px;
  height: 12px;
  border-radius: 2px;
}

.chart-legend-prompt {
  background: rgb(var(--v-theme-info));
  opacity: 0.7;
}

.chart-legend-completion {
  background: rgb(var(--v-theme-success));
  opacity: 0.8;
}

/* Log list */
.log-list {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.log-row {
  padding: 8px 12px;
  border-radius: 8px;
  cursor: pointer;
  transition: background 0.15s;
  border: 1px solid transparent;
}

.log-row:hover {
  background: rgba(var(--v-theme-on-surface), 0.04);
}

.log-row.active {
  background: rgba(var(--v-theme-primary), 0.08);
  border-color: rgba(var(--v-theme-primary), 0.2);
}

/* Detail content */
.content-pre {
  font-family: monospace;
  font-size: 11px;
  line-height: 1.5;
  background: rgba(var(--v-theme-on-surface), 0.05);
  padding: 8px;
  border-radius: 4px;
  white-space: pre-wrap;
  word-break: break-word;
  max-height: 300px;
  overflow-y: auto;
}
</style>
