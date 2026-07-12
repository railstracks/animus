<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import { apiGet } from '../lib/api';

interface Agent {
  id: string;
  name: string;
}

interface OntologyCategory {
  category: string;
  count: number;
}

interface OntologyEntity {
  id: number;
  parent_id: number | null;
  root_category: string;
  name: string;
  full_path: string;
  sort_order: number;
  created_at_unix_ms: number;
  updated_at_unix_ms: number;
}

interface OntologyProperty {
  id: number;
  entity_id: number;
  key: string;
  value: string;
  value_type: string;
  memory_state: 'new' | 'current' | 'deprecated' | string;
  linked_observation_id: number | null;
  created_at_unix_ms: number;
  updated_at_unix_ms: number;
}

interface OntologyEntityDetail extends OntologyEntity {
  properties: OntologyProperty[];
}

const { t } = useI18n();

const loading = ref(false);
const loadingDetail = ref(false);
const error = ref('');

const agents = ref<Agent[]>([]);
const selectedAgentId = ref('');

const categories = ref<OntologyCategory[]>([]);
const selectedCategory = ref('');
const entities = ref<OntologyEntity[]>([]);
const selectedEntityId = ref<number | null>(null);
const selectedEntity = ref<OntologyEntityDetail | null>(null);

const sortedEntities = computed(() =>
  [...entities.value].sort((a, b) => a.full_path.localeCompare(b.full_path))
);

function stateColor(state: string): string {
  if (state === 'current') return 'success';
  if (state === 'deprecated') return 'grey';
  return 'primary';
}

function stateLabel(state: string): string {
  if (state === 'current' || state === 'deprecated' || state === 'new') {
    return t(`ontology.states.${state}`);
  }
  return state;
}

function formatDate(unixMs: number): string {
  if (!unixMs) return 'n/a';
  const ts = Number(unixMs);
  if (Number.isNaN(ts) || ts <= 0) return 'n/a';
  return new Date(ts).toLocaleString();
}

function depthFor(entity: OntologyEntity): number {
  const parts = entity.full_path.split('/').filter((part) => part.length > 0);
  return Math.max(0, parts.length - 2);
}

function displayNameFor(entity: OntologyEntity): string {
  const parts = entity.full_path.split('/').filter((part) => part.length > 0);
  return parts.length > 0 ? parts[parts.length - 1] : entity.name;
}

async function fetchAgents(): Promise<void> {
  try {
    const data = await apiGet<{ agents: Agent[] }>('/api/v1/agents');
    agents.value = Array.isArray(data.agents)
      ? data.agents.filter((a) => a && typeof a.id === 'string' && a.id.length > 0)
          .map((a) => ({ id: a.id, name: (a.name || a.id).trim() || a.id }))
      : [];
    if (!agents.value.find((a) => a.id === selectedAgentId.value)) {
      selectedAgentId.value = agents.value[0]?.id || '';
    }
  } catch {
    agents.value = [];
  }
}

async function loadCategories(): Promise<void> {
  const params = selectedAgentId.value ? `?agent_id=${encodeURIComponent(selectedAgentId.value)}` : '';
  const payload = await apiGet<OntologyCategory[]>(`/api/v1/ontology/categories${params}`);
  categories.value = payload ?? [];
  if (!selectedCategory.value && categories.value.length > 0) {
    selectedCategory.value = categories.value[0].category;
  }
}

async function loadEntities(): Promise<void> {
  if (!selectedCategory.value) {
    entities.value = [];
    return;
  }
  let url = `/api/v1/ontology/entities?category=${encodeURIComponent(selectedCategory.value)}`;
  if (selectedAgentId.value) {
    url += `&agent_id=${encodeURIComponent(selectedAgentId.value)}`;
  }
  const payload = await apiGet<OntologyEntity[]>(url);
  entities.value = payload ?? [];
  if (selectedEntityId.value !== null) {
    const stillPresent = entities.value.some((entity) => entity.id === selectedEntityId.value);
    if (!stillPresent) {
      selectedEntityId.value = null;
      selectedEntity.value = null;
    }
  }
}

async function loadEntityDetail(entityId: number): Promise<void> {
  loadingDetail.value = true;
  try {
    const payload = await apiGet<OntologyEntityDetail>(`/api/v1/ontology/entities/${entityId}`);
    selectedEntity.value = payload;
  } finally {
    loadingDetail.value = false;
  }
}

async function refreshAll(): Promise<void> {
  loading.value = true;
  error.value = '';
  try {
    await fetchAgents();
    await loadCategories();
    await loadEntities();
    if (selectedEntityId.value !== null) {
      await loadEntityDetail(selectedEntityId.value);
    }
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load ontology';
  } finally {
    loading.value = false;
  }
}

function selectEntity(entity: OntologyEntity): void {
  selectedEntityId.value = entity.id;
  void loadEntityDetail(entity.id);
}

watch(selectedCategory, () => {
  selectedEntityId.value = null;
  selectedEntity.value = null;
  void loadEntities();
});

watch(selectedAgentId, () => {
  selectedEntityId.value = null;
  selectedEntity.value = null;
  void loadCategories();
});

onMounted(() => {
  void refreshAll();
});
</script>

<template>
  <v-row class="mb-3" align="center">
    <v-col cols="12" md="8">
      <h1 class="text-h5">{{ t('ontology.title') }}</h1>
      <div class="text-medium-emphasis">{{ t('ontology.subtitle') }}</div>
    </v-col>
    <v-col cols="12" md="4" class="text-md-end">
      <v-btn
        color="primary"
        prepend-icon="mdi-refresh"
        :loading="loading || loadingDetail"
        @click="refreshAll"
      >
        {{ t('ontology.actions.refresh') }}
      </v-btn>
    </v-col>
  </v-row>

  <v-alert v-if="error" type="error" variant="tonal" class="mb-4">
    {{ error }}
  </v-alert>

  <v-row>
    <v-col cols="12" md="4">
      <v-card variant="outlined" class="mb-4">
        <v-card-title>{{ t('ontology.sections.agent') }}</v-card-title>
        <v-card-text>
          <v-select
            v-model="selectedAgentId"
            :items="agents.map((a) => ({ title: a.name, value: a.id }))"
            item-title="title"
            item-value="value"
            density="comfortable"
            variant="outlined"
            hide-details
            clearable
          />
        </v-card-text>
      </v-card>

      <v-card variant="outlined" class="mb-4">
        <v-card-title>{{ t('ontology.sections.categories') }}</v-card-title>
        <v-card-text>
          <v-select
            v-model="selectedCategory"
            :items="categories.map((c) => ({ title: `${c.category} (${c.count})`, value: c.category }))"
            item-title="title"
            item-value="value"
            density="comfortable"
            variant="outlined"
            hide-details
          />
        </v-card-text>
      </v-card>

      <v-card variant="outlined">
        <v-card-title>{{ t('ontology.sections.entities') }}</v-card-title>
        <v-card-text class="pa-0">
          <v-list density="compact" v-if="sortedEntities.length > 0">
            <v-list-item
              v-for="entity in sortedEntities"
              :key="entity.id"
              :active="selectedEntityId === entity.id"
              @click="selectEntity(entity)"
            >
              <template #title>
                <div :style="{ paddingLeft: `${depthFor(entity) * 16}px` }">
                  {{ displayNameFor(entity) }}
                </div>
              </template>
              <template #subtitle>
                <span class="text-caption">{{ entity.full_path }}</span>
              </template>
            </v-list-item>
          </v-list>
          <div v-else class="pa-4 text-medium-emphasis">
            {{ t('ontology.empty.entities') }}
          </div>
        </v-card-text>
      </v-card>
    </v-col>

    <v-col cols="12" md="8">
      <v-card variant="outlined">
        <v-card-title>{{ t('ontology.sections.details') }}</v-card-title>
        <v-card-text v-if="selectedEntity">
          <div class="mb-4">
            <div><strong>ID:</strong> {{ selectedEntity.id }}</div>
            <div><strong>Category:</strong> {{ selectedEntity.root_category }}</div>
            <div><strong>Path:</strong> {{ selectedEntity.full_path }}</div>
            <div><strong>Updated:</strong> {{ formatDate(selectedEntity.updated_at_unix_ms) }}</div>
          </div>

          <h3 class="text-subtitle-1 mb-2">{{ t('ontology.sections.properties') }}</h3>
          <v-table density="comfortable">
            <thead>
              <tr>
                <th>Key</th>
                <th>Value</th>
                <th>Type</th>
                <th>State</th>
                <th>Observation</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="prop in selectedEntity.properties" :key="prop.id">
                <td>{{ prop.key }}</td>
                <td class="property-value">{{ prop.value }}</td>
                <td>{{ prop.value_type }}</td>
                <td>
                  <v-chip size="small" :color="stateColor(prop.memory_state)" variant="tonal">
                    {{ stateLabel(prop.memory_state) }}
                  </v-chip>
                </td>
                <td>{{ prop.linked_observation_id ?? 'n/a' }}</td>
              </tr>
              <tr v-if="selectedEntity.properties.length === 0">
                <td colspan="5" class="text-medium-emphasis">No properties yet.</td>
              </tr>
            </tbody>
          </v-table>
        </v-card-text>
        <v-card-text v-else class="text-medium-emphasis">
          {{ t('ontology.empty.details') }}
        </v-card-text>
      </v-card>
    </v-col>
  </v-row>
</template>

<style scoped>
.property-value {
  white-space: pre-wrap;
  word-break: break-word;
}
</style>
