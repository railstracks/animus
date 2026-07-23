<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';

import { apiGet, apiRequest } from '../lib/api';

// --- Types ---

interface Agent {
  id: string;
  name: string;
}

interface SearchResult {
  domain: string;
  id: number;
  text: string;
  relevance: number;
  layer_id: number | null;
  entity_path: string | null;
  source_path: string | null;
  layer: string | null;
  session_key: string | null;
}

// --- State ---

const { t } = useI18n();

const agents = ref<Agent[]>([]);
const selectedAgentId = ref<string>('');
const query = ref('');
const loading = ref(false);
const error = ref('');
const results = ref<SearchResult[]>([]);
const searchTimeMs = ref<number>(0);

const domains = ref({
  observations: true,
  ontology: true,
  raw_files: true,
  sessions: true
});

const limit = ref(20);

// --- Helpers ---

function domainLabel(domain: string): string {
  switch (domain) {
    case 'observation': return t('memorySearch.domains.episodic');
    case 'ontology': return t('memorySearch.domains.semantic');
    case 'memory_file': return t('memorySearch.domains.files');
    case 'session': return t('memorySearch.domains.sessions');
    default: return domain;
  }
}

function domainColor(domain: string): string {
  switch (domain) {
    case 'observation': return 'copper';
    case 'ontology': return 'teal';
    case 'memory_file': return 'info';
    case 'session': return 'warning';
    default: return 'grey';
  }
}

function domainIcon(domain: string): string {
  switch (domain) {
    case 'observation': return 'mdi-brain';
    case 'ontology': return 'mdi-graph-outline';
    case 'memory_file': return 'mdi-file-document-outline';
    case 'session': return 'mdi-chat-processing-outline';
    default: return 'mdi-magnify';
  }
}

function relevancePercent(relevance: number): number {
  return Math.round(relevance * 100);
}

function truncateText(text: string, maxLen = 300): string {
  if (text.length <= maxLen) return text;
  return text.substring(0, maxLen) + '…';
}

// --- API ---

async function loadAgents(): Promise<void> {
  try {
    const payload = await apiGet<{ agents: Agent[] }>('/api/v1/agents');
    agents.value = payload.agents ?? [];
    if (agents.value.length > 0 && !selectedAgentId.value) {
      selectedAgentId.value = agents.value[0].id;
    }
  } catch {
    agents.value = [];
  }
}

async function doSearch(): Promise<void> {
  const q = query.value.trim();
  if (!q) return;

  loading.value = true;
  error.value = '';
  results.value = [];
  const startTime = performance.now();

  try {
    const payload = await apiRequest<{ results?: SearchResult[]; count?: number }>(
      'POST',
      '/api/v1/memory/search',
      {
        query: q,
        agent_id: parseInt(selectedAgentId.value, 10) || 0,
        limit: limit.value,
        domains: {
          observations: domains.value.observations,
          ontology: domains.value.ontology,
          raw_files: domains.value.raw_files,
          diary: false,
          sessions: domains.value.sessions
        }
      }
    );
    results.value = payload.results ?? [];
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Search failed';
  } finally {
    searchTimeMs.value = Math.round(performance.now() - startTime);
    loading.value = false;
  }
}

// --- Domain group counts ---

function countByDomain(domain: string): number {
  return results.value.filter((r) => r.domain === domain).length;
}

onMounted(() => {
  void loadAgents();
});
</script>

<template>
  <v-row class="mb-3" align="center">
    <v-col cols="12" md="8">
      <h1 class="text-h5">{{ t('memorySearch.title') }}</h1>
      <div class="text-medium-emphasis">{{ t('memorySearch.subtitle') }}</div>
    </v-col>
  </v-row>

  <v-alert v-if="error" type="error" variant="tonal" closable class="mb-4" @click:close="error = ''">
    {{ error }}
  </v-alert>

  <!-- Search Controls -->
  <v-card variant="outlined" rounded="xl" class="mb-4 pa-4 search-card">
    <v-row align="start" dense>
      <v-col cols="12" md="4">
        <v-select
          v-model="selectedAgentId"
          :items="agents.map(a => ({ title: a.name, value: a.id }))"
          item-title="title"
          item-value="value"
          :label="t('memorySearch.agentLabel')"
          density="comfortable"
          variant="outlined"
          hide-details
        />
      </v-col>
      <v-col cols="12" md="8">
        <v-text-field
          v-model="query"
          :label="t('memorySearch.queryLabel')"
          density="comfortable"
          variant="outlined"
          hide-details
          clearable
          prepend-inner-icon="mdi-magnify"
          @keydown.enter="doSearch"
        >
          <template #append>
            <v-btn
              color="primary"
              variant="tonal"
              :loading="loading"
              @click="doSearch"
            >
              {{ t('memorySearch.searchButton') }}
            </v-btn>
          </template>
        </v-text-field>
      </v-col>
    </v-row>

    <v-row dense class="mt-2">
      <v-col cols="12" md="6">
        <div class="text-caption mb-1">{{ t('memorySearch.domainsLabel') }}</div>
        <div class="domain-checks">
          <v-checkbox
            v-model="domains.observations"
            :label="t('memorySearch.domains.episodic')"
            density="compact"
            hide-details
            color="copper"
          />
          <v-checkbox
            v-model="domains.ontology"
            :label="t('memorySearch.domains.semantic')"
            density="compact"
            hide-details
            color="teal"
          />
          <v-checkbox
            v-model="domains.raw_files"
            :label="t('memorySearch.domains.files')"
            density="compact"
            hide-details
          />
          <v-checkbox
            v-model="domains.sessions"
            :label="t('memorySearch.domains.sessions')"
            density="compact"
            hide-details
          />
        </div>
      </v-col>
      <v-col cols="12" md="3">
        <v-text-field
          v-model.number="limit"
          :label="t('memorySearch.limitLabel')"
          type="number"
          min="1"
          max="100"
          density="comfortable"
          variant="outlined"
          hide-details
        />
      </v-col>
    </v-row>
  </v-card>

  <!-- Results Summary -->
  <div v-if="results.length > 0" class="mb-3 results-summary">
    <span class="text-medium-emphasis">
      {{ results.length }} {{ t('memorySearch.resultsCount') }}
      &middot; {{ searchTimeMs }}ms
    </span>
    <v-chip
      v-if="countByDomain('observation') > 0"
      size="small" variant="tonal" color="copper" class="ml-2"
    >
      {{ countByDomain('observation') }} {{ t('memorySearch.domains.episodic') }}
    </v-chip>
    <v-chip
      v-if="countByDomain('ontology') > 0"
      size="small" variant="tonal" color="teal" class="ml-2"
    >
      {{ countByDomain('ontology') }} {{ t('memorySearch.domains.semantic') }}
    </v-chip>
    <v-chip
      v-if="countByDomain('memory_file') > 0"
      size="small" variant="tonal" color="info" class="ml-2"
    >
      {{ countByDomain('memory_file') }} {{ t('memorySearch.domains.files') }}
    </v-chip>
    <v-chip
      v-if="countByDomain('session') > 0"
      size="small" variant="tonal" color="warning" class="ml-2"
    >
      {{ countByDomain('session') }} {{ t('memorySearch.domains.sessions') }}
    </v-chip>
  </div>

  <!-- Empty States -->
  <div v-if="!loading && results.length === 0 && query && !error" class="text-center text-medium-emphasis pa-6">
    {{ t('memorySearch.noResults') }}
  </div>
  <div v-if="!loading && results.length === 0 && !query" class="text-center text-medium-emphasis pa-6">
    {{ t('memorySearch.placeholder') }}
  </div>

  <!-- Loading -->
  <div v-if="loading" class="text-center pa-6">
    <v-progress-circular indeterminate color="primary" size="32" />
    <div class="text-medium-emphasis mt-2">{{ t('memorySearch.searching') }}</div>
  </div>

  <!-- Results -->
  <div v-if="!loading && results.length > 0" class="results-list">
    <v-card
      v-for="(result, idx) in results"
      :key="idx"
      variant="outlined"
      rounded="lg"
      class="result-card mb-3"
    >
      <div class="result-header">
        <div class="result-domain">
          <v-icon size="small" :color="domainColor(result.domain)" class="mr-1">
            {{ domainIcon(result.domain) }}
          </v-icon>
          <v-chip size="small" variant="tonal" :color="domainColor(result.domain)">
            {{ domainLabel(result.domain) }}
          </v-chip>
        </div>
        <div class="result-meta">
          <span v-if="result.layer" class="meta-tag">{{ result.layer }}</span>
          <span v-if="result.entity_path" class="meta-tag">{{ result.entity_path }}</span>
          <span v-if="result.source_path" class="meta-tag">{{ result.source_path }}</span>
          <span v-if="result.session_key" class="meta-tag">{{ result.session_key }}</span>
          <span class="relevance-badge">{{ relevancePercent(result.relevance) }}%</span>
        </div>
      </div>
      <div class="result-body">
        {{ truncateText(result.text, 500) }}
      </div>
      <div class="result-footer">
        <span class="text-caption text-medium-emphasis">ID: {{ result.id }}</span>
      </div>
    </v-card>
  </div>
</template>

<style scoped>
.search-card {
  background: linear-gradient(170deg, rgba(23, 26, 35, 0.95), rgba(16, 18, 24, 0.98));
}

.domain-checks {
  display: flex;
  gap: 0.5rem;
  flex-wrap: wrap;
}

.domain-checks :deep(.v-checkbox) {
  flex: 0 0 auto;
}

.results-summary {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 0.25rem;
}

.result-card {
  padding: 0.75rem 1rem;
  background: linear-gradient(170deg, rgba(23, 26, 35, 0.95), rgba(16, 18, 24, 0.98));
  transition: border-color 0.15s;
}

.result-card:hover {
  border-color: rgba(var(--v-theme-on-surface), 0.15);
}

.result-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 0.75rem;
  margin-bottom: 0.5rem;
}

.result-domain {
  display: flex;
  align-items: center;
  gap: 0.25rem;
}

.result-meta {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  flex-wrap: wrap;
}

.meta-tag {
  font-size: 0.72rem;
  opacity: 0.6;
  background: rgba(var(--v-theme-on-surface), 0.05);
  padding: 0.1rem 0.4rem;
  border-radius: 4px;
}

.relevance-badge {
  font-size: 0.72rem;
  font-weight: 600;
  color: rgba(46, 196, 182, 0.8);
}

.result-body {
  font-size: 0.88rem;
  line-height: 1.45;
  white-space: pre-wrap;
  word-break: break-word;
  max-height: 200px;
  overflow-y: auto;
}

.result-footer {
  margin-top: 0.5rem;
}
</style>
