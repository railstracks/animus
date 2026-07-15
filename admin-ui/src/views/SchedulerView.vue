<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';

import { apiGet, apiRequest } from '../lib/api';

interface AgentSummary {
  id: string;
  name: string;
}

interface AgentsResponse {
  agents: AgentSummary[];
}

interface SchedulerStatusResponse {
  running: boolean;
}

interface ScheduleItem {
  id: string;
  agent_id: string;
  tag: string;
  schedule_type: 'one_shot' | 'recurring';
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

interface SchedulesResponse {
  schedules: ScheduleItem[];
}

const loadingStatus = ref(false);
const loadingAgents = ref(false);
const loadingSchedules = ref(false);
const deletingScheduleId = ref('');
const error = ref('');

const schedulerRunning = ref(false);
const agents = ref<AgentSummary[]>([]);
const selectedAgentId = ref('');
const tagFilter = ref('');
const includeDisabled = ref(true);
const schedules = ref<ScheduleItem[]>([]);

const agentItems = computed(() => {
  return agents.value.map((a) => ({
    value: a.id,
    title: `${a.name} (${a.id})`,
  }));
});

const totalCount = computed(() => schedules.value.length);
const enabledCount = computed(() => schedules.value.filter((item) => item.enabled).length);
const dueCount = computed(() =>
  schedules.value.filter((item) => item.enabled && isDue(item.next_fire)).length
);

function isDue(nextFireIso: string): boolean {
  const dueAt = Date.parse(nextFireIso);
  if (Number.isNaN(dueAt)) {
    return false;
  }
  return dueAt <= Date.now();
}

function formatDate(value: string): string {
  if (!value) {
    return 'n/a';
  }
  const ts = Date.parse(value);
  if (Number.isNaN(ts)) {
    return value;
  }
  return new Date(ts).toLocaleString();
}

async function loadStatus(): Promise<void> {
  loadingStatus.value = true;
  try {
    const status = await apiGet<SchedulerStatusResponse>('/api/v1/scheduler/status');
    schedulerRunning.value = Boolean(status.running);
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load scheduler status';
  } finally {
    loadingStatus.value = false;
  }
}

async function loadAgents(): Promise<void> {
  loadingAgents.value = true;
  try {
    const payload = await apiGet<AgentsResponse>('/api/v1/agents');
    agents.value = payload.agents ?? [];
    if (!selectedAgentId.value && agents.value.length > 0) {
      selectedAgentId.value = agents.value[0].id;
    }
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load agents';
  } finally {
    loadingAgents.value = false;
  }
}

async function loadSchedules(): Promise<void> {
  if (!selectedAgentId.value) {
    schedules.value = [];
    return;
  }

  loadingSchedules.value = true;
  error.value = '';
  try {
    const params = new URLSearchParams();
    params.set('agent_id', selectedAgentId.value);
    if (tagFilter.value.trim().length > 0) {
      params.set('tag', tagFilter.value.trim());
    }
    params.set('include_disabled', includeDisabled.value ? 'true' : 'false');
    const payload = await apiGet<SchedulesResponse>(`/api/v1/scheduler/schedules?${params.toString()}`);
    schedules.value = payload.schedules ?? [];
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load schedules';
    schedules.value = [];
  } finally {
    loadingSchedules.value = false;
  }
}

async function refreshAll(): Promise<void> {
  await Promise.all([loadStatus(), loadAgents()]);
  await loadSchedules();
}

async function deleteSchedule(id: string): Promise<void> {
  deletingScheduleId.value = id;
  error.value = '';
  try {
    await apiRequest('DELETE', `/api/v1/scheduler/schedules/${encodeURIComponent(id)}`);
    await loadSchedules();
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to cancel schedule';
  } finally {
    deletingScheduleId.value = '';
  }
}

onMounted(() => {
  void refreshAll();
});
</script>

<template>
  <v-row class="mb-3" align="center">
    <v-col cols="12" md="8">
      <h1 class="text-h5">Scheduler</h1>
      <div class="text-medium-emphasis">Review cron/one-shot schedules per agent and their next fire times.</div>
    </v-col>
    <v-col cols="12" md="4" class="text-md-end">
      <v-btn color="primary" prepend-icon="mdi-refresh" :loading="loadingStatus || loadingAgents || loadingSchedules" @click="refreshAll">
        Refresh
      </v-btn>
    </v-col>
  </v-row>

  <v-alert v-if="error" type="error" class="mb-4" variant="tonal">
    {{ error }}
  </v-alert>

  <v-row class="mb-4">
    <v-col cols="12" md="3">
      <v-card variant="outlined">
        <v-card-text>
          <div class="text-caption text-medium-emphasis">Scheduler State</div>
          <v-chip class="mt-2" :color="schedulerRunning ? 'success' : 'warning'" size="small" variant="flat">
            {{ schedulerRunning ? 'running' : 'stopped' }}
          </v-chip>
          <div class="mt-3 text-caption text-medium-emphasis">
            {{ loadingStatus ? 'Refreshing status...' : 'Live process status from kernel.' }}
          </div>
        </v-card-text>
      </v-card>
    </v-col>
    <v-col cols="12" md="3">
      <v-card variant="outlined">
        <v-card-text>
          <div class="text-caption text-medium-emphasis">Schedules Loaded</div>
          <div class="text-h5 mt-1">{{ totalCount }}</div>
          <div class="text-caption text-medium-emphasis mt-2">Enabled: {{ enabledCount }}</div>
          <div class="text-caption text-medium-emphasis">Due now: {{ dueCount }}</div>
        </v-card-text>
      </v-card>
    </v-col>
    <v-col cols="12" md="6">
      <v-card variant="outlined">
        <v-card-text>
          <v-row>
            <v-col cols="12" sm="6">
              <v-select
                v-model="selectedAgentId"
                label="Agent"
                :items="agentItems"
                :loading="loadingAgents"
                variant="outlined"
                density="comfortable"
                hide-details
                @update:model-value="loadSchedules"
              />
            </v-col>
            <v-col cols="12" sm="6">
              <v-text-field
                v-model="tagFilter"
                label="Tag filter"
                variant="outlined"
                density="comfortable"
                hide-details
                clearable
                @keyup.enter="loadSchedules"
                @click:clear="loadSchedules"
              />
            </v-col>
            <v-col cols="12" class="d-flex align-center justify-space-between">
              <v-switch
                v-model="includeDisabled"
                color="primary"
                hide-details
                label="Include disabled schedules"
                @update:model-value="loadSchedules"
              />
              <v-btn variant="tonal" size="small" prepend-icon="mdi-filter-check" @click="loadSchedules">
                Apply
              </v-btn>
            </v-col>
          </v-row>
        </v-card-text>
      </v-card>
    </v-col>
  </v-row>

  <v-card variant="outlined">
    <v-card-title class="d-flex align-center justify-space-between">
      <span>Schedules</span>
      <span class="text-caption text-medium-emphasis">Ordered by next fire</span>
    </v-card-title>
    <v-data-table
      :headers="[
        { title: 'ID', key: 'id' },
        { title: 'Type', key: 'schedule_type' },
        { title: 'Tag', key: 'tag' },
        { title: 'Next Fire', key: 'next_fire' },
        { title: 'Status', key: 'status' },
        { title: 'Fires', key: 'fire_count' },
        { title: 'Actions', key: 'actions', sortable: false }
      ]"
      :items="schedules"
      :loading="loadingSchedules"
      density="comfortable"
      item-key="id"
    >
      <template #item.id="{ item }">
        <span class="mono">{{ item.id }}</span>
      </template>
      <template #item.schedule_type="{ item }">
        <v-chip size="small" :color="item.schedule_type === 'recurring' ? 'indigo' : 'teal'" variant="tonal">
          {{ item.schedule_type }}
        </v-chip>
      </template>
      <template #item.tag="{ item }">
        <span>{{ item.tag || 'none' }}</span>
      </template>
      <template #item.next_fire="{ item }">
        <div>{{ formatDate(item.next_fire) }}</div>
        <div class="text-caption text-medium-emphasis">{{ item.next_fire }}</div>
      </template>
      <template #item.status="{ item }">
        <v-chip
          size="small"
          :color="!item.enabled ? 'grey' : (isDue(item.next_fire) ? 'warning' : 'success')"
          variant="tonal"
        >
          {{ !item.enabled ? 'disabled' : (isDue(item.next_fire) ? 'due' : 'scheduled') }}
        </v-chip>
      </template>
      <template #item.fire_count="{ item }">
        <span>{{ item.fire_count }}</span>
        <span v-if="item.max_fires > 0" class="text-medium-emphasis"> / {{ item.max_fires }}</span>
      </template>
      <template #item.actions="{ item }">
        <v-btn
          color="error"
          variant="text"
          size="small"
          prepend-icon="mdi-cancel"
          :loading="deletingScheduleId === item.id"
          @click="deleteSchedule(item.id)"
        >
          Cancel
        </v-btn>
      </template>
      <template #expanded-row="{ item }">
        <tr>
          <td colspan="7">
            <div class="py-2">
              <div><strong>Message:</strong> {{ item.message || 'none' }}</div>
              <div><strong>Cron:</strong> {{ item.cron_expr || 'n/a' }}</div>
              <div><strong>Timezone:</strong> {{ item.timezone || 'UTC' }}</div>
              <div><strong>Created:</strong> {{ formatDate(item.created_at) }}</div>
              <div><strong>Last Fire:</strong> {{ item.last_fire ? formatDate(item.last_fire) : 'never' }}</div>
            </div>
          </td>
        </tr>
      </template>
      <template #no-data>
        <div class="pa-6 text-medium-emphasis">No schedules found for this filter.</div>
      </template>
    </v-data-table>
  </v-card>
</template>

<style scoped>
.mono {
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace;
}
</style>
