<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import { apiGet, apiRequest } from '../lib/api';

// --- Types ---

interface AgentEntity {
  id: string;
  name: string;
}

interface SdtTags {
  autonomy?: number;
  competence?: number;
  relatedness?: number;
  personal_development?: number;
  relaxation?: number;
  meaning?: number;
}

interface GallivantingThread {
  id: number;
  name: string;
  description: string;
  sdt_tags: string[];
  enabled: boolean;
  created_at_unix_ms: number;
  updated_at_unix_ms: number;
}

interface GallivantingSession {
  id: number;
  thread_id: number;
  started_at_unix_ms: number;
  duration_ms: number;
  summary: string;
  outcome: string;
  sdt_scores: SdtTags;
  tools_used: string[];
  created_at_unix_ms: number;
}

interface SdtBalance {
  autonomy: number;
  competence: number;
  relatedness: number;
  personal_development: number;
  relaxation: number;
  meaning: number;
  session_count: number;
  since_unix_ms: number;
}

const SDT_KEYS: (keyof SdtTags)[] = [
  'autonomy', 'competence', 'relatedness',
  'personal_development', 'relaxation', 'meaning'
];

const SDT_LABELS: Record<string, string> = {
  autonomy: 'Autonomy',
  competence: 'Competence',
  relatedness: 'Relatedness',
  personal_development: 'Personal Dev',
  relaxation: 'Relaxation',
  meaning: 'Meaning'
};

const SDT_COLORS: Record<string, string> = {
  autonomy: '#4FC3F7',
  competence: '#81C784',
  relatedness: '#FFB74D',
  personal_development: '#CE93D8',
  relaxation: '#F06292',
  meaning: '#FFD54F'
};

// --- State ---

const { t } = useI18n();

const agents = ref<AgentEntity[]>([]);
const selectedAgentId = ref('default');
const loadingAgents = ref(false);
const error = ref('');

const threads = ref<GallivantingThread[]>([]);
const loadingThreads = ref(false);

const balance = ref<SdtBalance | null>(null);
const loadingBalance = ref(false);

const selectedThreadId = ref<number | null>(null);
const sessions = ref<GallivantingSession[]>([]);
const loadingSessions = ref(false);

// Lookback for spider graph
const lookbackDays = ref(14);
const lookbackOptions = [
  { title: '3 days', value: 3 },
  { title: '1 week', value: 7 },
  { title: '2 weeks', value: 14 },
  { title: '1 month', value: 30 },
  { title: '3 months', value: 90 }
];

// Thread dialog
const showThreadDialog = ref(false);
const editingThread = ref<GallivantingThread | null>(null);
const threadForm = ref({
  name: '',
  description: '',
  sdt_tags: [] as string[],
  enabled: true
});

// Delete confirm
const showDeleteConfirm = ref(false);
const deletingThreadId = ref<number | null>(null);

// Default prompt content (fetched from API)
const defaultPromptContent = ref('');

async function loadDefaultPrompt() {
  try {
    const locale = navigator.language?.split('-')[0] || 'en';
    const data = await apiGet<{ content: string }>(
      `/api/v1/memory/templates/gallivanting?locale=${locale}`
    );
    defaultPromptContent.value = data.content || '';
  } catch {
    defaultPromptContent.value = '';
  }
}

// --- Scheduling ---
interface ScheduleItem {
  id: string;
  agent_id: string;
  tag: string;
  schedule_type: string;
  next_fire: string;
  cron_expr: string;
  timezone: string;
  message: string;
  enabled: boolean;
  created_at: string;
  last_fire: string;
  fire_count: number;
  max_fires: number;
}

const scheduleId = ref('');
const loadingSchedule = ref(false);
const savingSchedule = ref(false);
const scheduleForm = ref({
  frequency: 'daily' as 'daily' | 'every_n_days' | 'weekly' | 'custom',
  every_n: 2,
  weekDay: 1,
  time: '02:00',
  timezone: Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC',
  duration_minutes: 60,
  random_offset_minutes: 30,
  prompt_template: '',
  enabled: true
});
const frequencyOptions = [
  { title: 'Daily', value: 'daily' },
  { title: 'Every N days', value: 'every_n_days' },
  { title: 'Weekly', value: 'weekly' },
  { title: 'Custom (cron)', value: 'custom' }
];
const weekDayOptions = [
  { title: 'Monday', value: 1 },
  { title: 'Tuesday', value: 2 },
  { title: 'Wednesday', value: 3 },
  { title: 'Thursday', value: 4 },
  { title: 'Friday', value: 5 },
  { title: 'Saturday', value: 6 },
  { title: 'Sunday', value: 0 }
];
const customCronExpr = ref('0 2 * * *');

// --- Spider Graph ---

function buildSpiderPath(scores: SdtTags, cx: number, cy: number, r: number): string {
  const n = SDT_KEYS.length;
  const points: string[] = [];
  for (let i = 0; i < n; i++) {
    const angle = (Math.PI * 2 * i) / n - Math.PI / 2;
    const val = Math.min(1, Math.max(0, scores[SDT_KEYS[i]] ?? 0));
    const x = cx + r * val * Math.cos(angle);
    const y = cy + r * val * Math.sin(angle);
    points.push(`${x},${y}`);
  }
  return `M ${points.join(' L ')} Z`;
}

function buildGridLines(cx: number, cy: number, r: number, levels: number = 4): string[] {
  const n = SDT_KEYS.length;
  const lines: string[] = [];
  for (let level = 1; level <= levels; level++) {
    const lr = (r * level) / levels;
    const points: string[] = [];
    for (let i = 0; i < n; i++) {
      const angle = (Math.PI * 2 * i) / n - Math.PI / 2;
      points.push(`${cx + lr * Math.cos(angle)},${cy + lr * Math.sin(angle)}`);
    }
    lines.push(`M ${points.join(' L ')} Z`);
  }
  return lines;
}

function axisPoint(index: number, cx: number, cy: number, r: number): { x: number; y: number } {
  const angle = (Math.PI * 2 * index) / SDT_KEYS.length - Math.PI / 2;
  return {
    x: cx + r * Math.cos(angle),
    y: cy + r * Math.sin(angle)
  };
}

const spiderSize = 280;
const spiderCx = spiderSize / 2;
const spiderCy = spiderSize / 2;
const spiderR = 110;
const gridPaths = computed(() => buildGridLines(spiderCx, spiderCy, spiderR));
const dataPath = computed(() => {
  if (!balance.value) return '';
  const scores: SdtTags = {
    autonomy: balance.value.autonomy,
    competence: balance.value.competence,
    relatedness: balance.value.relatedness,
    personal_development: balance.value.personal_development,
    relaxation: balance.value.relaxation,
    meaning: balance.value.meaning
  };
  return buildSpiderPath(scores, spiderCx, spiderCy, spiderR);
});

// --- Data ---

async function loadAgents() {
  loadingAgents.value = true;
  error.value = '';
  try {
    const data = await apiGet<{ agents: AgentEntity[] }>('/api/v1/agents');
    agents.value = data.agents || [];
    if (agents.value.length > 0 && selectedAgentId.value === 'default') {
      selectedAgentId.value = agents.value[0].id;
    }
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load agents';
  } finally {
    loadingAgents.value = false;
  }
}

async function loadThreads() {
  loadingThreads.value = true;
  error.value = '';
  try {
    const data = await apiGet<{ threads: GallivantingThread[]; count: number }>(
      `/api/v1/gallivanting/threads?agent_id=${encodeURIComponent(selectedAgentId.value)}`
    );
    threads.value = data.threads || [];
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load threads';
  } finally {
    loadingThreads.value = false;
  }
}

async function loadBalance() {
  loadingBalance.value = true;
  const sinceMs = Date.now() - lookbackDays.value * 24 * 60 * 60 * 1000;
  try {
    balance.value = await apiGet<SdtBalance>(
      `/api/v1/gallivanting/agents/${encodeURIComponent(selectedAgentId.value)}/balance?since=${sinceMs}`
    );
  } catch (e) {
    balance.value = null;
  } finally {
    loadingBalance.value = false;
  }
}

async function loadSessions(threadId: number) {
  selectedThreadId.value = threadId;
  loadingSessions.value = true;
  try {
    const data = await apiGet<{ sessions: GallivantingSession[]; count: number }>(
      `/api/v1/gallivanting/threads/${threadId}/sessions?limit=20`
    );
    sessions.value = data.sessions || [];
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load sessions';
    sessions.value = [];
  } finally {
    loadingSessions.value = false;
  }
}

async function loadAll() {
  await Promise.all([loadThreads(), loadBalance(), loadSchedule()]);
}

function onAgentChange() {
  selectedThreadId.value = null;
  sessions.value = [];
  loadAll();
}

watch(lookbackDays, () => loadBalance());

// --- Schedule ---

function frequencyToCron(freq: string, form: typeof scheduleForm.value): string {
  const [h, m] = form.time.split(':').map(Number);
  const hour = h ?? 2;
  const min = m ?? 0;
  switch (freq) {
    case 'daily': return `${min} ${hour} * * *`;
    case 'every_n_days': return `${min} ${hour} */${form.every_n} * *`;
    case 'weekly': return `${min} ${hour} * * ${form.weekDay}`;
    case 'custom': return customCronExpr.value;
    default: return `${min} ${hour} * * *`;
  }
}

async function loadSchedule() {
  loadingSchedule.value = true;
  scheduleId.value = '';
  try {
    const data = await apiGet<{ schedules: ScheduleItem[] }>(
      `/api/v1/scheduler/schedules?agent_id=${encodeURIComponent(selectedAgentId.value)}&tag=gallivanting`
    );
    const items = data.schedules || [];
    if (items.length > 0) {
      const sched = items[0];
      scheduleId.value = sched.id;
      scheduleForm.value.enabled = sched.enabled;

      // Parse cron back to form
      const cron = sched.cron_expr || '';
      customCronExpr.value = cron;
      const parts = cron.split(/\s+/);
      if (parts.length === 5) {
        const min = parseInt(parts[0]) || 0;
        const hour = parseInt(parts[1]) || 2;
        scheduleForm.value.time = `${String(hour).padStart(2, '0')}:${String(min).padStart(2, '0')}`;
        scheduleForm.value.timezone = sched.timezone || 'UTC';

        // Detect frequency type
        if (parts[4] !== '*' && parts[2] === '*') {
          scheduleForm.value.frequency = 'weekly';
          scheduleForm.value.weekDay = parseInt(parts[4]) || 1;
        } else if (parts[2].startsWith('*/')) {
          scheduleForm.value.frequency = 'every_n_days';
          scheduleForm.value.every_n = parseInt(parts[2].substring(2)) || 2;
        } else if (parts[4] === '*' && parts[2] === '*') {
          scheduleForm.value.frequency = 'daily';
        } else {
          scheduleForm.value.frequency = 'custom';
        }
      }

      // Parse duration from message JSON
      try {
        const msg = JSON.parse(sched.message || '{}');
        if (msg.duration_minutes) scheduleForm.value.duration_minutes = msg.duration_minutes;
        if (msg.random_offset_minutes) scheduleForm.value.random_offset_minutes = msg.random_offset_minutes;
        if (msg.prompt_template !== undefined) scheduleForm.value.prompt_template = msg.prompt_template;
      } catch {}
    }
  } catch {
    // No schedule yet — pre-populate prompt with default
    scheduleForm.value.prompt_template = defaultPromptContent.value;
  } finally {
    loadingSchedule.value = false;
  }
}

async function saveSchedule() {
  savingSchedule.value = true;
  error.value = '';
  try {
    const cronExpr = frequencyToCron(scheduleForm.value.frequency, scheduleForm.value);
    const message = JSON.stringify({
      type: 'gallivanting',
      duration_minutes: scheduleForm.value.duration_minutes,
      random_offset_minutes: scheduleForm.value.random_offset_minutes,
      prompt_template: scheduleForm.value.prompt_template || defaultPromptContent.value
    });

    const payload: Record<string, unknown> = {
      agent_id: selectedAgentId.value,
      tag: 'gallivanting',
      when: cronExpr,
      repeat: true,
      timezone: scheduleForm.value.timezone,
      message,
      enabled: scheduleForm.value.enabled
    };

    if (scheduleId.value) {
      payload.id = scheduleId.value;
    }

    const result = await apiRequest<ScheduleItem>('POST', '/api/v1/scheduler/schedules', payload);
    scheduleId.value = result.id;
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to save schedule';
  } finally {
    savingSchedule.value = false;
  }
}

function resetSchedulePrompt() {
  scheduleForm.value.prompt_template = defaultPromptContent.value;
}

async function deleteSchedule() {
  if (!scheduleId.value) return;
  error.value = '';
  try {
    await apiRequest('DELETE', `/api/v1/scheduler/schedules/${encodeURIComponent(scheduleId.value)}`);
    scheduleId.value = '';
    scheduleForm.value.enabled = true;
    scheduleForm.value.frequency = 'daily';
    scheduleForm.value.time = '02:00';
    scheduleForm.value.duration_minutes = 60;
    scheduleForm.value.random_offset_minutes = 30;
    scheduleForm.value.prompt_template = defaultPromptContent.value;
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to delete schedule';
  }
}

// --- Thread CRUD ---

function openCreateThread() {
  editingThread.value = null;
  threadForm.value = {
    name: '',
    description: '',
    sdt_tags: [],
    enabled: true
  };
  showThreadDialog.value = true;
}

function openEditThread(thread: GallivantingThread) {
  editingThread.value = thread;
  threadForm.value = {
    name: thread.name,
    description: thread.description,
    sdt_tags: [...thread.sdt_tags],
    enabled: thread.enabled
  };
  showThreadDialog.value = true;
}

function toggleSdtTag(key: string) {
  const idx = threadForm.value.sdt_tags.indexOf(key);
  if (idx >= 0) {
    threadForm.value.sdt_tags.splice(idx, 1);
  } else {
    threadForm.value.sdt_tags.push(key);
  }
}

async function saveThread() {
  error.value = '';
  const payload = {
    agent_id: selectedAgentId.value,
    name: threadForm.value.name,
    description: threadForm.value.description,
    sdt_tags: threadForm.value.sdt_tags,
    enabled: threadForm.value.enabled
  };

  try {
    if (editingThread.value) {
      await apiRequest('PATCH', `/api/v1/gallivanting/threads/${editingThread.value.id}`, payload);
    } else {
      await apiRequest('POST', '/api/v1/gallivanting/threads', payload);
    }
    showThreadDialog.value = false;
    await loadThreads();
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to save thread';
  }
}

function confirmDelete(threadId: number) {
  deletingThreadId.value = threadId;
  showDeleteConfirm.value = true;
}

async function doDelete() {
  if (!deletingThreadId.value) return;
  error.value = '';
  try {
    await apiRequest('DELETE', `/api/v1/gallivanting/threads/${deletingThreadId.value}`);
    showDeleteConfirm.value = false;
    if (selectedThreadId.value === deletingThreadId.value) {
      selectedThreadId.value = null;
      sessions.value = [];
    }
    deletingThreadId.value = null;
    await loadThreads();
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to delete thread';
  }
}


// --- Helpers ---

function formatDuration(ms: number): string {
  if (!ms) return '—';
  const seconds = Math.floor(ms / 1000);
  const minutes = Math.floor(seconds / 60);
  const hours = Math.floor(minutes / 60);
  if (hours > 0) return `${hours}h ${minutes % 60}m`;
  if (minutes > 0) return `${minutes}m ${seconds % 60}s`;
  return `${seconds}s`;
}

function formatDate(unixMs: number): string {
  if (!unixMs) return '—';
  return new Date(unixMs).toLocaleString();
}

function formatRelative(unixMs: number): string {
  if (!unixMs) return '';
  const diff = Date.now() - unixMs;
  const days = Math.floor(diff / (24 * 60 * 60 * 1000));
  if (days === 0) return 'today';
  if (days === 1) return 'yesterday';
  if (days < 7) return `${days}d ago`;
  if (days < 30) return `${Math.floor(days / 7)}w ago`;
  return `${Math.floor(days / 30)}mo ago`;
}

function sdtTagChips(tags: string[]): string[] {
  return tags || [];
}

const agentItems = computed(() =>
  agents.value.map(a => ({ title: a.name || a.id, value: a.id }))
);

onMounted(async () => {
  await loadAgents();
  await loadDefaultPrompt();
  await loadAll();
});
</script>

<template>
  <div class="gallivanting-layout">
    <!-- Error banner -->
    <v-alert
      v-if="error"
      type="error"
      variant="tonal"
      density="compact"
      closable
      class="mb-3"
      @click:close="error = ''"
    >
      {{ error }}
    </v-alert>

    <!-- Header -->
    <div class="page-header">
      <h2>Gallivanting</h2>
      <div class="header-controls">
        <v-select
          v-model="selectedAgentId"
          :items="agentItems"
          density="compact"
          variant="outlined"
          hide-details
          class="agent-select"
          @update:model-value="onAgentChange"
        />
        <v-btn size="small" variant="text" @click="loadAll">Refresh</v-btn>
      </div>
    </div>

    <div class="content-grid">
      <!-- Spider Graph Card -->
      <v-card rounded="xl" class="spider-card">
        <div class="card-header">
          <h3>SDT Balance</h3>
          <v-select
            v-model="lookbackDays"
            :items="lookbackOptions"
            density="compact"
            variant="outlined"
            hide-details
            class="lookback-select"
          />
        </div>

        <div v-if="loadingBalance" class="loading-state">Loading...</div>
        <div v-else-if="!balance || balance.session_count === 0" class="empty-state">
          <p>No gallivanting sessions yet</p>
          <p class="hint">Sessions will appear here once the agent starts gallivanting</p>
        </div>
        <div v-else class="spider-container">
          <svg :width="spiderSize" :height="spiderSize" :viewBox="`0 0 ${spiderSize} ${spiderSize}`">
            <!-- Grid -->
            <path
              v-for="(path, i) in gridPaths"
              :key="'grid-' + i"
              :d="path"
              fill="none"
              stroke="rgba(255,255,255,0.08)"
              stroke-width="1"
            />
            <!-- Axes -->
            <line
              v-for="(_, i) in SDT_KEYS"
              :key="'axis-' + i"
              :x1="spiderCx"
              :y1="spiderCy"
              :x2="axisPoint(i, spiderCx, spiderCy, spiderR).x"
              :y2="axisPoint(i, spiderCx, spiderCy, spiderR).y"
              stroke="rgba(255,255,255,0.06)"
              stroke-width="1"
            />
            <!-- Data area -->
            <path
              :d="dataPath"
              fill="rgba(79, 195, 247, 0.15)"
              stroke="#4FC3F7"
              stroke-width="2"
            />
            <!-- Labels -->
            <text
              v-for="(key, i) in SDT_KEYS"
              :key="'label-' + i"
              :x="axisPoint(i, spiderCx, spiderCy, spiderR + 18).x"
              :y="axisPoint(i, spiderCx, spiderCy, spiderR + 18).y"
              text-anchor="middle"
              dominant-baseline="central"
              fill="rgba(255,255,255,0.7)"
              font-size="11"
            >
              {{ SDT_LABELS[key] }}
            </text>
          </svg>
          <div class="balance-meta">
            {{ balance.session_count }} sessions in last {{ lookbackDays }} days
          </div>
        </div>
      </v-card>

      <!-- Threads Card -->
      <v-card rounded="xl" class="threads-card">
        <div class="card-header">
          <h3>Threads</h3>
          <v-btn size="small" color="primary" variant="tonal" prepend-icon="mdi-plus" @click="openCreateThread">
            New Thread
          </v-btn>
        </div>

        <div v-if="loadingThreads" class="loading-state">Loading...</div>
        <div v-else-if="threads.length === 0" class="empty-state">
          <p>No threads configured</p>
          <p class="hint">Create a thread to give the agent exploration options</p>
        </div>
        <div v-else class="threads-list">
          <div
            v-for="thread in threads"
            :key="thread.id"
            class="thread-item"
            :class="{ selected: selectedThreadId === thread.id, disabled: !thread.enabled }"
            @click="loadSessions(thread.id)"
          >
            <div class="thread-main">
              <div class="thread-name">
                <v-icon size="16" :color="thread.enabled ? 'primary' : 'grey'">
                  {{ thread.enabled ? 'mdi-radiobox-marked' : 'mdi-radiobox-blank' }}
                </v-icon>
                {{ thread.name }}
              </div>
              <div v-if="thread.description" class="thread-desc">{{ thread.description }}</div>
              <div class="thread-tags">
                <span
                  v-for="tag in sdtTagChips(thread.sdt_tags)"
                  :key="tag"
                  class="sdt-chip"
                  :style="{ background: SDT_COLORS[tag] + '22', color: SDT_COLORS[tag] }"
                >
                  {{ SDT_LABELS[tag] }}
                </span>
              </div>
            </div>
            <div class="thread-actions">
              <v-btn icon size="x-small" variant="text" @click.stop="openEditThread(thread)">
                <v-icon size="16">mdi-pencil</v-icon>
              </v-btn>
              <v-btn icon size="x-small" variant="text" color="error" @click.stop="confirmDelete(thread.id)">
                <v-icon size="16">mdi-delete</v-icon>
              </v-btn>
            </div>
          </div>
        </div>
      </v-card>
    </div>

    <!-- Schedule Card -->
    <v-card rounded="xl" class="schedule-card">
      <div class="card-header">
        <h3>Schedule</h3>
        <div class="schedule-status">
          <v-chip
            v-if="scheduleId"
            size="small"
            :color="scheduleForm.enabled ? 'success' : 'grey'"
            variant="tonal"
          >
            {{ scheduleForm.enabled ? 'Active' : 'Paused' }}
          </v-chip>
          <v-chip v-else size="small" color="grey" variant="tonal">Not scheduled</v-chip>
        </div>
      </div>

      <div v-if="loadingSchedule" class="loading-state">Loading schedule...</div>
      <div v-else class="schedule-form">
        <div class="schedule-row">
          <v-select
            v-model="scheduleForm.frequency"
            :items="frequencyOptions"
            label="Frequency"
            density="compact"
            variant="outlined"
            hide-details
            class="schedule-field"
          />

          <v-select
            v-if="scheduleForm.frequency === 'weekly'"
            v-model="scheduleForm.weekDay"
            :items="weekDayOptions"
            label="Day"
            density="compact"
            variant="outlined"
            hide-details
            class="schedule-field"
          />

          <v-text-field
            v-if="scheduleForm.frequency === 'every_n_days'"
            v-model.number="scheduleForm.every_n"
            label="Every N days"
            type="number"
            min="1"
            density="compact"
            variant="outlined"
            hide-details
            class="schedule-field-narrow"
          />
        </div>

        <div v-if="scheduleForm.frequency === 'custom'" class="schedule-row">
          <v-text-field
            v-model="customCronExpr"
            label="Cron expression"
            density="compact"
            variant="outlined"
            hide-details
            class="schedule-field"
            placeholder="0 2 * * *"
          />
        </div>

        <div class="schedule-row">
          <v-text-field
            v-model="scheduleForm.time"
            label="Time (HH:MM)"
            type="time"
            density="compact"
            variant="outlined"
            hide-details
            class="schedule-field-narrow"
          />

          <v-select
            v-model="scheduleForm.timezone"
            :items="Intl.supportedValuesOf ? Intl.supportedValuesOf('timeZone') : ['UTC', 'Europe/Amsterdam', 'Europe/Berlin', 'America/New_York']"
            label="Timezone"
            density="compact"
            variant="outlined"
            hide-details
            class="schedule-field"
          />
        </div>

        <div class="schedule-row">
          <v-text-field
            v-model.number="scheduleForm.duration_minutes"
            label="Duration (minutes)"
            type="number"
            min="5"
            density="compact"
            variant="outlined"
            hide-details
            class="schedule-field-narrow"
          />

          <v-text-field
            v-model.number="scheduleForm.random_offset_minutes"
            label="Random offset (min)"
            type="number"
            min="0"
            density="compact"
            variant="outlined"
            hide-details
            class="schedule-field-narrow"
            hint="Avoids mechanical regularity"
          />

          <v-switch
            v-model="scheduleForm.enabled"
            label="Enabled"
            density="compact"
            color="primary"
            hide-details
          />
        </div>

        <div class="prompt-section">
          <div class="form-section-label">Prompt Template</div>
          <v-textarea
            v-model="scheduleForm.prompt_template"
            :placeholder="defaultPromptContent || 'Loading default prompt...'"
            density="comfortable"
            variant="outlined"
            rows="6"
            class="mb-1"
          />
          <v-btn size="x-small" variant="text" @click="resetSchedulePrompt">
            Reset to default
          </v-btn>
        </div>

        <div class="schedule-actions">
          <v-btn
            size="small"
            color="primary"
            variant="tonal"
            :loading="savingSchedule"
            @click="saveSchedule"
          >
            {{ scheduleId ? 'Update Schedule' : 'Create Schedule' }}
          </v-btn>
          <v-btn
            v-if="scheduleId"
            size="small"
            variant="text"
            color="error"
            @click="deleteSchedule"
          >
            Remove
          </v-btn>
        </div>
      </div>
    </v-card>

    <!-- Session History (shown when a thread is selected) -->
    <v-card v-if="selectedThreadId" rounded="xl" class="sessions-card">
      <div class="card-header">
        <h3>
          Sessions — {{ threads.find(t => t.id === selectedThreadId)?.name || 'Thread' }}
        </h3>
        <v-btn size="x-small" variant="text" @click="selectedThreadId = null; sessions = []">
          Close
        </v-btn>
      </div>

      <div v-if="loadingSessions" class="loading-state">Loading sessions...</div>
      <div v-else-if="sessions.length === 0" class="empty-state">
        <p>No sessions recorded for this thread yet</p>
      </div>
      <div v-else class="sessions-list">
        <div v-for="session in sessions" :key="session.id" class="session-item">
          <div class="session-header">
            <span class="session-date">{{ formatDate(session.created_at_unix_ms) }}</span>
            <span class="session-duration">{{ formatDuration(session.duration_ms) }}</span>
            <span class="session-relative">{{ formatRelative(session.created_at_unix_ms) }}</span>
          </div>
          <div class="session-summary">{{ session.summary }}</div>
          <div v-if="session.outcome" class="session-outcome">
            <strong>Outcome:</strong> {{ session.outcome }}
          </div>
          <div class="session-meta">
            <span
              v-for="key in SDT_KEYS.filter(k => session.sdt_scores[k] !== undefined && session.sdt_scores[k]! > 0)"
              :key="key"
              class="sdt-chip small"
              :style="{ background: SDT_COLORS[key] + '22', color: SDT_COLORS[key] }"
            >
              {{ SDT_LABELS[key] }} {{ session.sdt_scores[key]?.toFixed(1) }}
            </span>
            <span v-if="session.tools_used?.length" class="tools-used">
              <v-icon size="12">mdi-tools</v-icon> {{ session.tools_used.join(', ') }}
            </span>
          </div>
        </div>
      </div>
    </v-card>

    <!-- Thread Create/Edit Dialog -->
    <v-dialog v-model="showThreadDialog" max-width="560px">
      <v-card rounded="xl">
        <v-card-title>{{ editingThread ? 'Edit Thread' : 'New Thread' }}</v-card-title>
        <v-card-text>
          <v-text-field
            v-model="threadForm.name"
            label="Thread Name"
            density="comfortable"
            variant="outlined"
            class="mb-3"
          />
          <v-textarea
            v-model="threadForm.description"
            label="Description / Current State"
            density="comfortable"
            variant="outlined"
            rows="2"
            class="mb-3"
          />

          <div class="form-section-label">SDT Need Tags</div>
          <div class="sdt-toggles">
            <div
              v-for="key in SDT_KEYS"
              :key="key"
              class="sdt-toggle"
              :class="{ active: threadForm.sdt_tags.includes(key) }"
              :style="{
                borderColor: threadForm.sdt_tags.includes(key) ? SDT_COLORS[key] : 'rgba(255,255,255,0.1)',
                background: threadForm.sdt_tags.includes(key) ? SDT_COLORS[key] + '15' : 'transparent'
              }"
              @click="toggleSdtTag(key)"
            >
              <span class="sdt-toggle-label" :style="{ color: threadForm.sdt_tags.includes(key) ? SDT_COLORS[key] : 'rgba(255,255,255,0.5)' }">
                {{ SDT_LABELS[key] }}
              </span>
            </div>
          </div>

          <div class="mt-3">
            <v-switch
              v-model="threadForm.enabled"
              label="Enabled"
              density="compact"
              color="primary"
              hide-details
            />
          </div>
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="showThreadDialog = false">Cancel</v-btn>
          <v-btn
            color="primary"
            :disabled="!threadForm.name.trim()"
            @click="saveThread"
          >
            {{ editingThread ? 'Save' : 'Create' }}
          </v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Delete Confirm -->
    <v-dialog v-model="showDeleteConfirm" max-width="360px">
      <v-card rounded="xl">
        <v-card-title>Delete Thread?</v-card-title>
        <v-card-text>
          This will delete the thread and all its session history. This cannot be undone.
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="showDeleteConfirm = false">Cancel</v-btn>
          <v-btn color="error" @click="doDelete">Delete</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </div>
</template>

<style scoped>
.gallivanting-layout {
  max-width: 960px;
  margin: 0 auto;
}

.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 1.25rem;
}

.page-header h2 {
  font-size: 1rem;
  margin: 0;
}

.header-controls {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.agent-select {
  width: 180px;
}

.content-grid {
  display: grid;
  grid-template-columns: 320px 1fr;
  gap: 1rem;
}

@media (max-width: 768px) {
  .content-grid {
    grid-template-columns: 1fr;
  }
}

.spider-card,
.threads-card,
.sessions-card,
.schedule-card {
  padding: 1.25rem;
  background: linear-gradient(170deg, rgba(23, 26, 35, 0.95), rgba(16, 18, 24, 0.98));
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 1rem;
}

.card-header h3 {
  font-size: 0.85rem;
  margin: 0;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  opacity: 0.8;
}

.lookback-select {
  width: 120px;
}

.loading-state,
.empty-state {
  text-align: center;
  opacity: 0.6;
  padding: 2rem 1rem;
}

.empty-state .hint {
  font-size: 0.8rem;
  opacity: 0.5;
  margin-top: 0.5rem;
}

.spider-container {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.balance-meta {
  font-size: 0.75rem;
  opacity: 0.5;
  margin-top: 0.5rem;
}

.threads-list {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.thread-item {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  padding: 0.75rem;
  border-radius: 10px;
  border: 1px solid rgba(255, 255, 255, 0.06);
  cursor: pointer;
  transition: all 0.15s ease;
}

.thread-item:hover {
  border-color: rgba(255, 255, 255, 0.12);
  background: rgba(255, 255, 255, 0.02);
}

.thread-item.selected {
  border-color: rgba(79, 195, 247, 0.3);
  background: rgba(79, 195, 247, 0.05);
}

.thread-item.disabled {
  opacity: 0.5;
}

.thread-main {
  flex: 1;
  min-width: 0;
}

.thread-name {
  display: flex;
  align-items: center;
  gap: 0.4rem;
  font-weight: 500;
  font-size: 0.9rem;
}

.thread-desc {
  font-size: 0.78rem;
  opacity: 0.6;
  margin-top: 0.25rem;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.thread-tags {
  display: flex;
  gap: 0.3rem;
  flex-wrap: wrap;
  margin-top: 0.4rem;
}

.sdt-chip {
  display: inline-block;
  padding: 0.1rem 0.45rem;
  border-radius: 4px;
  font-size: 0.68rem;
  font-weight: 500;
  letter-spacing: 0.02em;
}

.sdt-chip.small {
  font-size: 0.62rem;
  padding: 0.05rem 0.35rem;
}

.thread-actions {
  display: flex;
  gap: 0;
  opacity: 0;
  transition: opacity 0.15s ease;
}

.thread-item:hover .thread-actions {
  opacity: 1;
}

.sessions-card {
  margin-top: 1rem;
}

.sessions-list {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.session-item {
  padding: 0.75rem;
  border-radius: 10px;
  border: 1px solid rgba(255, 255, 255, 0.06);
}

.session-header {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  margin-bottom: 0.5rem;
}

.session-date {
  font-size: 0.8rem;
  opacity: 0.7;
}

.session-duration {
  font-size: 0.75rem;
  opacity: 0.5;
  font-variant-numeric: tabular-nums;
}

.session-relative {
  font-size: 0.72rem;
  opacity: 0.4;
  margin-left: auto;
}

.session-summary {
  font-size: 0.88rem;
  line-height: 1.5;
}

.session-outcome {
  font-size: 0.82rem;
  opacity: 0.7;
  margin-top: 0.4rem;
  line-height: 1.5;
}

.session-meta {
  display: flex;
  gap: 0.4rem;
  flex-wrap: wrap;
  margin-top: 0.5rem;
  align-items: center;
}

.tools-used {
  display: flex;
  align-items: center;
  gap: 0.25rem;
  font-size: 0.7rem;
  opacity: 0.5;
}

.form-section-label {
  font-size: 0.78rem;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  opacity: 0.6;
  margin-bottom: 0.5rem;
}

.sdt-toggles {
  display: flex;
  flex-wrap: wrap;
  gap: 0.4rem;
}

.sdt-toggle {
  padding: 0.35rem 0.7rem;
  border-radius: 8px;
  border: 1px solid;
  cursor: pointer;
  transition: all 0.15s ease;
  user-select: none;
}

.sdt-toggle:hover {
  filter: brightness(1.2);
}

.sdt-toggle-label {
  font-size: 0.78rem;
  font-weight: 500;
}

/* Schedule */

.schedule-card {
  margin-top: 1rem;
}

.schedule-status {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.schedule-form {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.schedule-row {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  flex-wrap: wrap;
}

.schedule-field {
  flex: 1;
  min-width: 140px;
}

.schedule-field-narrow {
  width: 130px;
  min-width: 100px;
}

.schedule-actions {
  display: flex;
  gap: 0.75rem;
  margin-top: 0.5rem;
}

.prompt-section {
  margin-top: 0.25rem;
}
</style>
