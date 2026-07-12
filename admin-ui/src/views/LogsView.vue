<script setup lang="ts">
import { onMounted, onUnmounted, ref, computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { apiGet } from '../lib/api';

const { t } = useI18n();

interface LogResponse {
  lines: string[];
  total_lines: number;
  offset: number;
  returned: number;
}

const lines = ref<string[]>([]);
const totalLines = ref(0);
const currentOffset = ref(0);
const loading = ref(false);
const error = ref('');
const autoRefresh = ref(true);
const maxDisplayLines = ref(500);
let refreshTimer: ReturnType<typeof setInterval> | null = null;

// Filter
const filterText = ref('');
const levelFilter = ref<string>('');

const filteredLines = computed(() => {
  let result = lines.value;
  if (levelFilter.value) {
    const level = levelFilter.value.toLowerCase();
    result = result.filter(l => {
      const lower = l.toLowerCase();
      return lower.includes(`[${level}]`) || lower.includes(`(${level})`) ||
             lower.includes(` ${level} `) || lower.includes(`${level}:`);
    });
  }
  if (filterText.value.trim()) {
    const q = filterText.value.toLowerCase();
    result = result.filter(l => l.toLowerCase().includes(q));
  }
  return result;
});

const levelColors: Record<string, string> = {
  error: 'text-error',
  warn: 'text-warning',
  warning: 'text-warning',
  info: 'text-info',
  debug: 'text-medium-emphasis',
  trace: 'text-disabled',
};

function getLineClass(line: string): string {
  const lower = line.toLowerCase();
  if (lower.includes('[error]') || lower.includes('error:') || lower.includes('[fatal]'))
    return levelColors.error;
  if (lower.includes('[warn') || lower.includes('warning:'))
    return levelColors.warn;
  if (lower.includes('[debug]'))
    return levelColors.debug;
  if (lower.includes('[trace]'))
    return levelColors.trace;
  return '';
}

async function loadLogs() {
  loading.value = true;
  error.value = '';
  try {
    const params = new URLSearchParams();
    params.set('lines', String(maxDisplayLines.value));
    if (currentOffset.value > 0) params.set('offset', String(currentOffset.value));
    const data = await apiGet<LogResponse>(`/api/v1/logs?${params}`);
    lines.value = data.lines ?? [];
    totalLines.value = data.total_lines ?? 0;
    currentOffset.value = data.offset ?? 0;
  } catch (e: any) {
    error.value = e.message || t('logs.failed');
  } finally {
    loading.value = false;
  }
}

async function refresh() {
  try {
    // Just fetch the tail — last 500 lines
    const params = new URLSearchParams();
    params.set('lines', String(maxDisplayLines.value));
    const data = await apiGet<LogResponse>(`/api/v1/logs?${params}`);
    lines.value = data.lines ?? [];
    totalLines.value = data.total_lines ?? 0;
  } catch (_e) {
    // Silent fail on refresh
  }
}

function toggleAutoRefresh() {
  autoRefresh.value = !autoRefresh.value;
  if (autoRefresh.value) {
    startTimer();
  } else {
    stopTimer();
  }
}

function startTimer() {
  if (refreshTimer) clearInterval(refreshTimer);
  refreshTimer = setInterval(refresh, 5000);
}

function stopTimer() {
  if (refreshTimer) {
    clearInterval(refreshTimer);
    refreshTimer = null;
  }
}

function clearFilter() {
  filterText.value = '';
  levelFilter.value = '';
}

const hasFilter = computed(() => filterText.value.trim() || levelFilter.value);

onMounted(() => {
  loadLogs();
  if (autoRefresh.value) startTimer();
});

onUnmounted(() => {
  stopTimer();
});
</script>

<template>
  <div>
    <!-- Toolbar -->
    <v-card rounded="xl" class="mb-4 pa-3">
      <div class="d-flex align-center flex-wrap ga-3">
        <div class="text-h6">{{ t('logs.title') }}</div>
        <v-chip size="small" variant="tonal">{{ totalLines }} {{ t('logs.linesTotal') }}</v-chip>
        <v-spacer />

        <!-- Level filter -->
        <v-select
          v-model="levelFilter"
          :items="['', 'error', 'warn', 'info', 'debug']"
          :label="t('logs.level')"
          density="compact"
          hide-details
          style="max-width: 140px;"
          clearable
        />

        <!-- Text filter -->
        <v-text-field
          v-model="filterText"
          :label="t('logs.filter')"
          density="compact"
          hide-details
          prepend-inner-icon="mdi-magnify"
          style="max-width: 220px;"
          clearable
        />

        <!-- Auto-refresh toggle -->
        <v-btn
          :variant="autoRefresh ? 'flat' : 'tonal'"
          :color="autoRefresh ? 'success' : ''"
          size="small"
          @click="toggleAutoRefresh"
        >
          <v-icon start>{{ autoRefresh ? 'mdi-pause' : 'mdi-play' }}</v-icon>
          {{ autoRefresh ? t('logs.live') : t('logs.paused') }}
        </v-btn>

        <!-- Manual refresh -->
        <v-btn variant="tonal" size="small" prepend-icon="mdi-refresh" @click="loadLogs">
          {{ t('logs.reload') }}
        </v-btn>
      </div>
    </v-card>

    <!-- Error -->
    <v-alert v-if="error" type="error" variant="tonal" rounded="xl" class="mb-4" closable>
      {{ error }}
    </v-alert>

    <!-- Log output -->
    <v-card rounded="xl" class="log-card">
      <v-card-text class="log-container">
        <div v-if="loading && lines.length === 0" class="text-center py-4 text-medium-emphasis">
          {{ t('logs.loading') }}
        </div>
        <div v-else-if="filteredLines.length === 0" class="text-center py-4 text-medium-emphasis">
          <span v-if="hasFilter">{{ t('logs.noMatch') }}</span>
          <span v-else>{{ t('logs.empty') }}</span>
        </div>
        <pre v-else class="log-output"><span
          v-for="(line, i) in filteredLines"
          :key="i"
          :class="getLineClass(line)"
          class="log-line"
        >{{ line }}</span></pre>
      </v-card-text>
    </v-card>
  </div>
</template>

<style scoped>
.log-card {
  max-height: 70vh;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}

.log-container {
  max-height: 65vh;
  overflow-y: auto;
  padding: 0.5rem;
  font-family: monospace;
}

.log-output {
  margin: 0;
  font-size: 0.8rem;
  line-height: 1.4;
  white-space: pre-wrap;
  word-break: break-all;
}

.log-line {
  display: block;
  padding: 1px 0.5rem;
  border-radius: 2px;
}
.log-line:hover {
  background: rgba(var(--v-theme-on-surface), 0.04);
}

/* Level colors */
.text-error { color: rgb(var(--v-theme-error)); }
.text-warning { color: #FF9800; }
.text-info { color: rgb(var(--v-theme-info)); }
.text-debug { color: rgba(var(--v-theme-on-surface), 0.5); }
.text-disabled { color: rgba(var(--v-theme-on-surface), 0.3); }
.text-medium-emphasis { color: rgba(var(--v-theme-on-surface), 0.6); }
</style>
