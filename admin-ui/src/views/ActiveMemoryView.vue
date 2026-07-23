<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';

import { apiGet } from '../lib/api';

interface ContextBlock {
  name: string;
  content: string;
  priority: number;
}

interface SessionFlags {
  is_synthetic: boolean;
  include_episodic: boolean;
  include_diary: boolean;
  include_ontology: boolean;
}

interface ActiveMemoryResponse {
  rendered: string;
  agent_id: string;
  is_synthetic_session: boolean;
  session_key?: string;
  blocks: ContextBlock[];
  block_count: number;
  flags: SessionFlags;
}

interface AgentInfo {
  id: string;
  name: string;
}

interface SessionInfo {
  id: number;
  key: {
    connector: string;
    conversation_id: string;
    thread_id: string;
  };
}

const { t } = useI18n();

const loading = ref(false);
const error = ref('');
const agents = ref<AgentInfo[]>([]);
const sessions = ref<SessionInfo[]>([]);

const selectedAgentId = ref('');
const selectedSessionKey = ref('');
const response = ref<ActiveMemoryResponse | null>(null);

const expandedBlocks = ref<Set<string>>(new Set());

const sortedBlocks = computed(() =>
  [...(response.value?.blocks ?? [])].sort((a, b) => a.priority - b.priority)
);

function toggleBlock(name: string): void {
  if (expandedBlocks.value.has(name)) {
    expandedBlocks.value.delete(name);
  } else {
    expandedBlocks.value.add(name);
  }
}

function formatSessionLabel(session: SessionInfo): string {
  const k = session.key;
  const parts = [k.connector, k.conversation_id];
  if (k.thread_id) parts.push(k.thread_id);
  return `#${session.id} ${parts.join(' | ')}`;
}

async function loadAgents(): Promise<void> {
  const payload = await apiGet<{ agents: AgentInfo[] }>('/api/v1/agents');
  agents.value = payload?.agents ?? [];
  if (!selectedAgentId.value && agents.value.length > 0) {
    selectedAgentId.value = agents.value[0].id;
  }
}

async function loadSessions(): Promise<void> {
  const payload = await apiGet<{ sessions: SessionInfo[] }>('/api/v1/sessions');
  sessions.value = payload?.sessions ?? [];
}

async function loadActiveMemory(): Promise<void> {
  loading.value = true;
  error.value = '';
  expandedBlocks.value.clear();

  try {
    let url = `/api/v1/context/active-memory?agent_id=${encodeURIComponent(selectedAgentId.value)}`;
    if (selectedSessionKey.value) {
      url += `&session_key=${encodeURIComponent(selectedSessionKey.value)}`;
    }
    response.value = await apiGet<ActiveMemoryResponse>(url);
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load active memory';
    response.value = null;
  } finally {
    loading.value = false;
  }
}

async function refreshAll(): Promise<void> {
  loading.value = true;
  error.value = '';
  try {
    await loadAgents();
    await loadSessions();
    if (selectedAgentId.value) {
      await loadActiveMemory();
    }
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to initialize';
  } finally {
    loading.value = false;
  }
}

onMounted(() => {
  void refreshAll();
});
</script>

<template>
  <v-row class="mb-3" align="center">
    <v-col cols="12" md="8">
      <h1 class="text-h5">{{ t('activeMemory.title') }}</h1>
      <div class="text-medium-emphasis">{{ t('activeMemory.subtitle') }}</div>
    </v-col>
    <v-col cols="12" md="4" class="text-md-end">
      <v-btn
        color="primary"
        prepend-icon="mdi-refresh"
        :loading="loading"
        @click="loadActiveMemory"
      >
        {{ t('activeMemory.actions.refresh') }}
      </v-btn>
    </v-col>
  </v-row>

  <!-- Controls -->
  <v-row class="mb-4">
    <v-col cols="12" md="4">
      <v-select
        v-model="selectedAgentId"
        :items="agents.map((a) => ({ title: `${a.name} (${a.id})`, value: a.id }))"
        item-title="title"
        item-value="value"
        :label="t('activeMemory.labels.agent')"
        density="comfortable"
        variant="outlined"
        hide-details
        @update:model-value="loadActiveMemory"
      />
    </v-col>
    <v-col cols="12" md="6">
      <v-select
        v-model="selectedSessionKey"
        :items="[
          { title: t('activeMemory.labels.syntheticSession'), value: '' },
          ...sessions.map((s) => ({ title: formatSessionLabel(s), value: String(s.id) }))
        ]"
        item-title="title"
        item-value="value"
        :label="t('activeMemory.labels.session')"
        density="comfortable"
        variant="outlined"
        hide-details
        clearable
        @update:model-value="loadActiveMemory"
      />
    </v-col>
  </v-row>

  <v-alert v-if="error" type="error" variant="tonal" class="mb-4">
    {{ error }}
  </v-alert>

  <template v-if="response">
    <!-- Flags indicator -->
    <v-chip-group class="mb-4">
      <v-chip
        size="small"
        :color="response.is_synthetic_session ? 'warning' : 'success'"
        variant="tonal"
      >
        {{ response.is_synthetic_session
          ? t('activeMemory.flags.synthetic')
          : t('activeMemory.flags.live') }}
      </v-chip>
      <v-chip
        v-for="(active, key) in response.flags"
        :key="key"
        v-show="key !== 'is_synthetic'"
        size="small"
        :color="active ? 'primary' : 'grey'"
        variant="tonal"
      >
        {{ key }}: {{ active ? '✓' : '✗' }}
      </v-chip>
      <v-chip size="small" variant="tonal">
        {{ response.block_count }} {{ t('activeMemory.labels.blocks') }}
      </v-chip>
    </v-chip-group>

    <!-- Rendered preview -->
    <v-card variant="outlined" class="mb-4">
      <v-card-title class="d-flex align-center">
        {{ t('activeMemory.sections.rendered') }}
        <v-spacer />
        <v-chip size="x-small" variant="tonal">
          {{ response.rendered.length }} chars
        </v-chip>
      </v-card-title>
      <v-card-text>
        <pre class="rendered-preview">{{ response.rendered }}</pre>
      </v-card-text>
    </v-card>

    <!-- Per-block breakdown -->
    <v-card variant="outlined">
      <v-card-title>{{ t('activeMemory.sections.blocks') }}</v-card-title>
      <v-card-text>
        <v-expansion-panels variant="accordion">
          <v-expansion-panel
            v-for="block in sortedBlocks"
            :key="block.name"
            @click="toggleBlock(block.name)"
          >
            <v-expansion-panel-title>
              <div class="d-flex align-center ga-2">
                <v-chip size="small" variant="tonal" color="primary">
                  P{{ block.priority }}
                </v-chip>
                <strong>{{ block.name }}</strong>
                <span class="text-medium-emphasis text-caption">
                  ({{ block.content.length }} chars)
                </span>
              </div>
            </v-expansion-panel-title>
            <v-expansion-panel-text>
              <pre class="block-content">{{ block.content }}</pre>
            </v-expansion-panel-text>
          </v-expansion-panel>
        </v-expansion-panels>
      </v-card-text>
    </v-card>
  </template>

  <v-card v-else-if="!loading" variant="outlined" class="pa-4 text-center text-medium-emphasis">
    {{ t('activeMemory.empty') }}
  </v-card>
</template>

<style scoped>
.rendered-preview,
.block-content {
  white-space: pre-wrap;
  word-break: break-word;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-size: 0.85em;
  line-height: 1.5;
  max-height: 400px;
  overflow-y: auto;
  background: rgba(var(--v-theme-on-surface), 0.03);
  padding: 12px;
  border-radius: 6px;
}
</style>
