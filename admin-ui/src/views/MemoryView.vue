<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import { apiGet, apiRequest } from '../lib/api';

interface MemoryLayer {
  name: string;
  description: string;
  retention_policy: string;
  horizon: string;
  consolidation_interval: string;
  perspective_past: string;
  perspective_current: string;
  perspective_future: string;
  observation_count: number;
}

interface MemoryLayersResponse {
  layers?: MemoryLayer[];
}

interface MemoryObservation {
  id: string;
  content: string;
  tags: string[];
  timestamp_unix_ms: number;
  weight: number;
  decay_rate: number;
  layer: string;
  created_in_layer: string;
  demotion_reason?: string | null;
  demotion_timestamp_unix_ms?: number | null;
  age_seconds: number;
}

interface LayerObservationsResponse {
  layer?: string;
  page?: number;
  limit?: number;
  total?: number;
  sort?: string;
  order?: string;
  observations?: MemoryObservation[];
}

interface ConsolidationStatusResponse {
  state?: 'idle' | 'running';
  active_job_id?: number;
  last_run_unix_ms?: number;
  last_job_id?: number;
  last_summary?: {
    promoted?: number;
    demoted?: number;
    archived?: number;
  };
  last_error?: string | null;
}

const { t } = useI18n();

const layers = ref<MemoryLayer[]>([]);
const selectedLayerName = ref('');
const expandedLayerPanels = ref<string[]>([]);
const loadingLayers = ref(false);
const layersError = ref('');

const observations = ref<MemoryObservation[]>([]);
const loadingObservations = ref(false);
const observationsError = ref('');

const selectedObservationId = ref('');
const selectedObservation = ref<MemoryObservation | null>(null);
const loadingObservationDetail = ref(false);
const detailError = ref('');

const page = ref(1);
const limit = ref(25);
const total = ref(0);
const sortBy = ref<'weight' | 'age' | 'tags'>('weight');
const sortOrder = ref<'asc' | 'desc'>('desc');
const tagFilter = ref<string[]>([]);
const compactMode = ref(false);

const editWeight = ref(1);
const editTags = ref<string[]>([]);
const detailActionError = ref('');
const detailActionSuccess = ref('');
const savingDetail = ref(false);
const archivingObservation = ref(false);

const consolidationStatus = ref<ConsolidationStatusResponse | null>(null);
const loadingConsolidation = ref(false);
const consolidationError = ref('');
const triggeringConsolidation = ref(false);
let consolidationPollTimer: number | null = null;

const pageSizeItems = [10, 25, 50, 100];

const selectedLayer = computed(
  () => layers.value.find((layer) => layer.name === selectedLayerName.value) ?? null
);

const maxLayerCount = computed(() => {
  if (layers.value.length === 0) {
    return 1;
  }
  return Math.max(...layers.value.map((layer) => layer.observation_count || 0), 1);
});

const filteredObservations = computed(() => {
  let items = observations.value.slice();

  const filters = tagFilter.value
    .map((tag) => tag.trim().toLowerCase())
    .filter((tag) => tag.length > 0);

  if (filters.length > 0) {
    items = items.filter((observation) => {
      const tags = observation.tags.map((tag) => tag.toLowerCase());
      return filters.every((tag) => tags.includes(tag));
    });
  }

  if (sortBy.value === 'tags') {
    items.sort((a, b) => {
      const aTags = a.tags.join(',');
      const bTags = b.tags.join(',');
      if (aTags === bTags) {
        return 0;
      }
      if (sortOrder.value === 'asc') {
        return aTags.localeCompare(bTags);
      }
      return bTags.localeCompare(aTags);
    });
  }

  return items;
});

const totalPages = computed(() => Math.max(1, Math.ceil(total.value / limit.value)));

const sortItems = computed(() => [
  { title: t('memory.list.sort.weight'), value: 'weight' },
  { title: t('memory.list.sort.age'), value: 'age' },
  { title: t('memory.list.sort.tags'), value: 'tags' }
]);

const orderItems = computed(() => [
  { title: t('memory.list.order.desc'), value: 'desc' },
  { title: t('memory.list.order.asc'), value: 'asc' }
]);

const tagOptions = computed(() => {
  const tags = new Set<string>();
  for (const observation of observations.value) {
    for (const tag of observation.tags) {
      if (tag.trim().length > 0) {
        tags.add(tag.trim());
      }
    }
  }
  return Array.from(tags).sort((a, b) => a.localeCompare(b));
});

function formatUnixMs(value?: number | null): string {
  if (!value || value <= 0) {
    return t('memory.common.notAvailable');
  }
  return new Date(value).toLocaleString();
}

function formatAge(seconds: number): string {
  if (seconds < 60) {
    return `${seconds.toFixed(0)}s`;
  }
  if (seconds < 3600) {
    return `${(seconds / 60).toFixed(1)}m`;
  }
  if (seconds < 86400) {
    return `${(seconds / 3600).toFixed(1)}h`;
  }
  return `${(seconds / 86400).toFixed(1)}d`;
}

function normalizeTags(tags: string[]): string[] {
  const unique = new Set<string>();
  for (const tag of tags) {
    const trimmed = tag.trim();
    if (trimmed.length > 0) {
      unique.add(trimmed);
    }
  }
  return Array.from(unique);
}

function layerDensityValue(layer: MemoryLayer): number {
  return Math.round(((layer.observation_count || 0) / maxLayerCount.value) * 100);
}

function decayPercent(observation: MemoryObservation): number {
  const base = Math.max(0, observation.decay_rate);
  const curve = Math.pow(base, Math.max(0, observation.age_seconds) / 3600);
  return Math.min(100, Math.max(0, curve * 100));
}

function resetDetailMessages(): void {
  detailActionError.value = '';
  detailActionSuccess.value = '';
}

async function loadLayers(): Promise<void> {
  loadingLayers.value = true;
  layersError.value = '';
  try {
    const payload = await apiGet<MemoryLayersResponse>('/api/v1/memory/layers');
    layers.value = Array.isArray(payload.layers) ? payload.layers : [];

    if (!selectedLayerName.value && layers.value.length > 0) {
      selectedLayerName.value = layers.value[0].name;
    }

    if (selectedLayerName.value && !layers.value.some((layer) => layer.name === selectedLayerName.value)) {
      selectedLayerName.value = layers.value.length > 0 ? layers.value[0].name : '';
    }

    if (selectedLayerName.value.length > 0 && expandedLayerPanels.value.length === 0) {
      expandedLayerPanels.value = [selectedLayerName.value];
    }
  } catch (err) {
    layersError.value = err instanceof Error ? err.message : t('common.unknownError');
  } finally {
    loadingLayers.value = false;
  }
}

async function loadObservations(): Promise<void> {
  if (!selectedLayerName.value) {
    observations.value = [];
    total.value = 0;
    return;
  }

  loadingObservations.value = true;
  observationsError.value = '';

  const serverSort = sortBy.value === 'age' ? 'age' : 'weight';
  const query = new URLSearchParams({
    page: String(page.value),
    limit: String(limit.value),
    sort: serverSort,
    order: sortOrder.value
  });

  try {
    const payload = await apiGet<LayerObservationsResponse>(
      `/api/v1/memory/layers/${encodeURIComponent(selectedLayerName.value)}/observations?${query.toString()}`
    );
    observations.value = Array.isArray(payload.observations) ? payload.observations : [];
    total.value = typeof payload.total === 'number' ? payload.total : 0;

    if (selectedObservationId.value) {
      const stillVisible = observations.value.some((item) => item.id === selectedObservationId.value);
      if (!stillVisible) {
        selectedObservation.value = null;
      }
    }
  } catch (err) {
    observationsError.value = err instanceof Error ? err.message : t('common.unknownError');
    observations.value = [];
    total.value = 0;
  } finally {
    loadingObservations.value = false;
  }
}

async function loadObservationDetail(observationId: string): Promise<void> {
  if (!observationId) {
    selectedObservation.value = null;
    return;
  }

  loadingObservationDetail.value = true;
  detailError.value = '';
  resetDetailMessages();

  try {
    const payload = await apiGet<MemoryObservation>(`/api/v1/memory/observations/${encodeURIComponent(observationId)}`);
    selectedObservation.value = payload;
    editWeight.value = Number(payload.weight || 0);
    editTags.value = payload.tags.slice();
  } catch (err) {
    detailError.value = err instanceof Error ? err.message : t('common.unknownError');
    selectedObservation.value = null;
  } finally {
    loadingObservationDetail.value = false;
  }
}

async function saveObservationEdits(): Promise<void> {
  if (!selectedObservation.value) {
    return;
  }

  savingDetail.value = true;
  resetDetailMessages();

  try {
    const normalizedWeight = Number(editWeight.value);
    const payload = await apiRequest<MemoryObservation>(
      'PATCH',
      `/api/v1/memory/observations/${encodeURIComponent(selectedObservation.value.id)}`,
      {
        weight: normalizedWeight,
        tags: normalizeTags(editTags.value)
      }
    );

    selectedObservation.value = payload;
    editWeight.value = Number(payload.weight || normalizedWeight);
    editTags.value = payload.tags.slice();

    observations.value = observations.value.map((item) => (item.id === payload.id ? payload : item));
    detailActionSuccess.value = t('memory.detail.saveSuccess');
  } catch (err) {
    detailActionError.value = err instanceof Error ? err.message : t('common.unknownError');
  } finally {
    savingDetail.value = false;
  }
}

function resetObservationEdits(): void {
  if (!selectedObservation.value) {
    return;
  }
  editWeight.value = Number(selectedObservation.value.weight || 0);
  editTags.value = selectedObservation.value.tags.slice();
  resetDetailMessages();
}

async function archiveObservation(): Promise<void> {
  if (!selectedObservation.value) {
    return;
  }

  archivingObservation.value = true;
  resetDetailMessages();

  try {
    await apiRequest('DELETE', `/api/v1/memory/observations/${encodeURIComponent(selectedObservation.value.id)}`);
    detailActionSuccess.value = t('memory.detail.archiveSuccess');
    selectedObservationId.value = '';
    selectedObservation.value = null;
    await Promise.all([loadLayers(), loadObservations()]);
  } catch (err) {
    detailActionError.value = err instanceof Error ? err.message : t('common.unknownError');
  } finally {
    archivingObservation.value = false;
  }
}

async function loadConsolidationStatus(): Promise<void> {
  loadingConsolidation.value = true;
  consolidationError.value = '';

  try {
    consolidationStatus.value = await apiGet<ConsolidationStatusResponse>(
      '/api/v1/memory/consolidation/status'
    );
  } catch (err) {
    consolidationError.value = err instanceof Error ? err.message : t('common.unknownError');
  } finally {
    loadingConsolidation.value = false;
  }
}

async function triggerConsolidation(): Promise<void> {
  triggeringConsolidation.value = true;
  consolidationError.value = '';
  try {
    await apiRequest('POST', '/api/v1/memory/consolidate');
    await loadConsolidationStatus();
  } catch (err) {
    consolidationError.value = err instanceof Error ? err.message : t('common.unknownError');
  } finally {
    triggeringConsolidation.value = false;
  }
}

function consolidationStateLabel(): string {
  if (consolidationStatus.value?.state === 'running') {
    return t('memory.consolidation.state.running');
  }
  return t('memory.consolidation.state.idle');
}

function selectLayer(name: string): void {
  if (selectedLayerName.value === name) {
    return;
  }
  selectedLayerName.value = name;
  page.value = 1;
  selectedObservationId.value = '';
  selectedObservation.value = null;
}

function selectObservation(observationId: string): void {
  if (selectedObservationId.value === observationId) {
    return;
  }
  selectedObservationId.value = observationId;
}

function startConsolidationPolling(): void {
  if (consolidationPollTimer !== null) {
    return;
  }
  consolidationPollTimer = window.setInterval(() => {
    void loadConsolidationStatus();
  }, 3000);
}

function stopConsolidationPolling(): void {
  if (consolidationPollTimer !== null) {
    window.clearInterval(consolidationPollTimer);
    consolidationPollTimer = null;
  }
}

watch([selectedLayerName, page, limit, sortOrder, sortBy], () => {
  void loadObservations();
});

watch(selectedObservationId, (observationId) => {
  if (!observationId) {
    selectedObservation.value = null;
    return;
  }
  void loadObservationDetail(observationId);
});

watch(
  () => consolidationStatus.value?.state,
  (state) => {
    if (state === 'running') {
      startConsolidationPolling();
      return;
    }
    stopConsolidationPolling();
  }
);

onMounted(async () => {
  await Promise.all([loadLayers(), loadConsolidationStatus()]);
  await loadObservations();
});

onBeforeUnmount(() => {
  stopConsolidationPolling();
});
</script>

<template>
  <div class="memory-layout">
    <aside class="layers-column">
      <v-card rounded="xl" class="layers-card">
        <div class="card-header">
          <h2>{{ t('memory.layers.title') }}</h2>
          <v-btn size="small" variant="text" :loading="loadingLayers" @click="loadLayers">
            {{ t('memory.actions.refresh') }}
          </v-btn>
        </div>

        <v-alert
          v-if="layersError"
          type="error"
          variant="tonal"
          density="comfortable"
          class="mb-3"
        >
          {{ layersError }}
        </v-alert>

        <v-expansion-panels
          v-model="expandedLayerPanels"
          multiple
          variant="accordion"
          class="layers-panels"
        >
          <v-expansion-panel
            v-for="layer in layers"
            :key="layer.name"
            :value="layer.name"
          >
            <v-expansion-panel-title>
              <div class="layer-title-row">
                <div class="layer-name">{{ layer.name }}</div>
                <v-chip size="x-small" color="primary" variant="tonal">
                  {{ layer.observation_count }}
                </v-chip>
              </div>
            </v-expansion-panel-title>
            <v-expansion-panel-text>
              <p class="layer-description">{{ layer.description }}</p>
              <v-progress-linear
                :model-value="layerDensityValue(layer)"
                height="8"
                rounded
                color="info"
                class="mb-3"
              />
              <div class="layer-meta">
                <p><strong>{{ t('memory.layers.horizon') }}:</strong> {{ layer.horizon }}</p>
                <p>
                  <strong>{{ t('memory.layers.consolidationInterval') }}:</strong>
                  {{ layer.consolidation_interval }}
                </p>
                <p><strong>{{ t('memory.layers.retentionPolicy') }}:</strong> {{ layer.retention_policy }}</p>
                <p><strong>{{ t('memory.layers.perspectiveCurrent') }}:</strong> {{ layer.perspective_current }}</p>
                <p><strong>{{ t('memory.layers.perspectivePast') }}:</strong> {{ layer.perspective_past }}</p>
                <p><strong>{{ t('memory.layers.perspectiveFuture') }}:</strong> {{ layer.perspective_future }}</p>
              </div>
              <v-btn
                size="small"
                variant="tonal"
                color="primary"
                :disabled="selectedLayerName === layer.name"
                @click="selectLayer(layer.name)"
              >
                {{ t('memory.layers.viewObservations') }}
              </v-btn>
            </v-expansion-panel-text>
          </v-expansion-panel>
        </v-expansion-panels>

        <p v-if="!loadingLayers && layers.length === 0" class="empty-note">
          {{ t('memory.layers.empty') }}
        </p>
      </v-card>

      <v-card rounded="xl" class="consolidation-card">
        <div class="card-header">
          <h2>{{ t('memory.consolidation.title') }}</h2>
          <v-chip
            size="small"
            :color="consolidationStatus?.state === 'running' ? 'warning' : 'success'"
            variant="tonal"
          >
            {{ consolidationStateLabel() }}
          </v-chip>
        </div>

        <v-alert
          v-if="consolidationError"
          type="error"
          variant="tonal"
          density="comfortable"
          class="mb-3"
        >
          {{ consolidationError }}
        </v-alert>

        <div class="consolidation-meta">
          <p>
            <strong>{{ t('memory.consolidation.activeJob') }}:</strong>
            {{ consolidationStatus?.active_job_id || t('memory.common.notAvailable') }}
          </p>
          <p>
            <strong>{{ t('memory.consolidation.lastRun') }}:</strong>
            {{ formatUnixMs(consolidationStatus?.last_run_unix_ms) }}
          </p>
          <p>
            <strong>{{ t('memory.consolidation.lastJob') }}:</strong>
            {{ consolidationStatus?.last_job_id || t('memory.common.notAvailable') }}
          </p>
          <p>
            <strong>{{ t('memory.consolidation.promoted') }}:</strong>
            {{ consolidationStatus?.last_summary?.promoted || 0 }}
          </p>
          <p>
            <strong>{{ t('memory.consolidation.demoted') }}:</strong>
            {{ consolidationStatus?.last_summary?.demoted || 0 }}
          </p>
          <p>
            <strong>{{ t('memory.consolidation.archived') }}:</strong>
            {{ consolidationStatus?.last_summary?.archived || 0 }}
          </p>
        </div>

        <p v-if="consolidationStatus?.last_error" class="consolidation-error">
          {{ consolidationStatus.last_error }}
        </p>

        <v-btn
          color="secondary"
          block
          :loading="triggeringConsolidation"
          :disabled="consolidationStatus?.state === 'running'"
          @click="triggerConsolidation"
        >
          {{ t('memory.actions.triggerConsolidation') }}
        </v-btn>
      </v-card>
    </aside>

    <section class="list-column">
      <v-card rounded="xl" class="observations-card">
        <div class="card-header list-header">
          <div>
            <h2>{{ t('memory.list.title') }}</h2>
            <p class="subtitle" v-if="selectedLayer">
              {{ t('memory.list.activeLayer', { layer: selectedLayer.name }) }}
            </p>
          </div>
          <v-btn size="small" variant="text" :loading="loadingObservations" @click="loadObservations">
            {{ t('memory.actions.refresh') }}
          </v-btn>
        </div>

        <div class="filters-row">
          <v-select
            v-model="sortBy"
            :items="sortItems"
            :label="t('memory.list.sortBy')"
            density="compact"
            variant="outlined"
            hide-details
            class="control-sort"
          />
          <v-select
            v-model="sortOrder"
            :items="orderItems"
            :label="t('memory.list.orderBy')"
            density="compact"
            variant="outlined"
            hide-details
            class="control-order"
          />
          <v-combobox
            v-model="tagFilter"
            :items="tagOptions"
            multiple
            chips
            closable-chips
            clearable
            :label="t('memory.list.tagFilter')"
            density="compact"
            variant="outlined"
            hide-details
            class="control-tags"
          />
          <v-select
            v-model="limit"
            :items="pageSizeItems"
            :label="t('memory.list.pageSize')"
            density="compact"
            variant="outlined"
            hide-details
            class="control-limit"
          />
          <v-switch
            v-model="compactMode"
            inset
            density="compact"
            hide-details
            :label="t('memory.list.compactMode')"
            class="control-compact"
          />
        </div>

        <v-alert
          v-if="observationsError"
          type="error"
          variant="tonal"
          density="comfortable"
          class="mb-3"
        >
          {{ observationsError }}
        </v-alert>

        <div class="table-wrap">
          <v-table density="comfortable" fixed-header height="520px">
            <thead>
              <tr>
                <th>{{ t('memory.list.columns.content') }}</th>
                <th>{{ t('memory.list.columns.tags') }}</th>
                <th>{{ t('memory.list.columns.weight') }}</th>
                <th>{{ t('memory.list.columns.age') }}</th>
                <th>{{ t('memory.list.columns.decay') }}</th>
                <th>{{ t('memory.list.columns.actions') }}</th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="observation in filteredObservations"
                :key="observation.id"
                :class="{ 'active-row': selectedObservationId === observation.id }"
                @click="selectObservation(observation.id)"
              >
                <td class="content-col">
                  <div class="content-text">{{ observation.content }}</div>
                </td>
                <td>
                  <div class="tags-wrap">
                    <v-chip
                      v-for="tag in observation.tags"
                      :key="`${observation.id}-${tag}`"
                      size="x-small"
                      variant="tonal"
                      color="primary"
                    >
                      {{ tag }}
                    </v-chip>
                  </div>
                </td>
                <td>{{ observation.weight.toFixed(2) }}</td>
                <td>{{ formatAge(observation.age_seconds) }}</td>
                <td>
                  <v-progress-linear
                    :model-value="decayPercent(observation)"
                    height="8"
                    rounded
                    color="warning"
                  />
                </td>
                <td>
                  <v-btn size="x-small" variant="text" @click.stop="selectObservation(observation.id)">
                    {{ t('memory.list.openDetail') }}
                  </v-btn>
                </td>
              </tr>
            </tbody>
          </v-table>
        </div>

        <p v-if="!loadingObservations && observations.length === 0" class="empty-note">
          {{ t('memory.list.emptyLayer') }}
        </p>
        <p v-else-if="!loadingObservations && filteredObservations.length === 0" class="empty-note">
          {{ t('memory.list.emptyFilter') }}
        </p>

        <div class="pagination-row">
          <v-pagination v-model="page" :length="totalPages" total-visible="7" />
        </div>
      </v-card>
    </section>

    <aside class="detail-column">
      <v-card rounded="xl" class="detail-card">
        <div class="card-header">
          <h2>{{ t('memory.detail.title') }}</h2>
        </div>

        <p v-if="!selectedObservationId" class="empty-note">
          {{ t('memory.detail.empty') }}
        </p>

        <v-progress-linear
          v-if="loadingObservationDetail"
          indeterminate
          color="primary"
          class="mb-3"
        />

        <v-alert
          v-if="detailError"
          type="error"
          variant="tonal"
          density="comfortable"
          class="mb-3"
        >
          {{ detailError }}
        </v-alert>

        <template v-if="selectedObservation">
          <div class="detail-section">
            <p class="detail-label">{{ t('memory.detail.content') }}</p>
            <div class="detail-content">{{ selectedObservation.content }}</div>
          </div>

          <div class="detail-meta">
            <p><strong>ID:</strong> {{ selectedObservation.id }}</p>
            <p><strong>{{ t('memory.detail.layer') }}:</strong> {{ selectedObservation.layer }}</p>
            <p>
              <strong>{{ t('memory.detail.createdInLayer') }}:</strong>
              {{ selectedObservation.created_in_layer }}
            </p>
            <p>
              <strong>{{ t('memory.detail.timestamp') }}:</strong>
              {{ formatUnixMs(selectedObservation.timestamp_unix_ms) }}
            </p>
            <p>
              <strong>{{ t('memory.detail.demotionReason') }}:</strong>
              {{ selectedObservation.demotion_reason || t('memory.common.notAvailable') }}
            </p>
            <p>
              <strong>{{ t('memory.detail.demotionTimestamp') }}:</strong>
              {{ formatUnixMs(selectedObservation.demotion_timestamp_unix_ms || null) }}
            </p>
          </div>

          <div class="detail-edit">
            <v-text-field
              v-model.number="editWeight"
              type="number"
              min="0.01"
              max="1000"
              step="0.01"
              density="comfortable"
              variant="outlined"
              :label="t('memory.detail.editWeight')"
            />
            <v-combobox
              v-model="editTags"
              multiple
              chips
              closable-chips
              clearable
              density="comfortable"
              variant="outlined"
              :items="tagOptions"
              :label="t('memory.detail.editTags')"
            />
          </div>

          <v-alert
            v-if="detailActionError"
            type="error"
            variant="tonal"
            density="comfortable"
            class="mb-3"
          >
            {{ detailActionError }}
          </v-alert>

          <v-alert
            v-if="detailActionSuccess"
            type="success"
            variant="tonal"
            density="comfortable"
            class="mb-3"
          >
            {{ detailActionSuccess }}
          </v-alert>

          <div class="detail-actions">
            <v-btn
              color="primary"
              :loading="savingDetail"
              @click="saveObservationEdits"
            >
              {{ t('memory.detail.save') }}
            </v-btn>
            <v-btn
              variant="text"
              :disabled="savingDetail || archivingObservation"
              @click="resetObservationEdits"
            >
              {{ t('memory.detail.reset') }}
            </v-btn>
            <v-btn
              color="error"
              variant="tonal"
              :loading="archivingObservation"
              :disabled="savingDetail"
              @click="archiveObservation"
            >
              {{ t('memory.detail.archive') }}
            </v-btn>
          </div>
        </template>
      </v-card>
    </aside>
  </div>
</template>

<style scoped>
.memory-layout {
  display: grid;
  grid-template-columns: 320px minmax(0, 1fr) 360px;
  gap: 1rem;
}

.layers-column,
.list-column,
.detail-column {
  min-height: calc(100vh - 160px);
}

.layers-card,
.observations-card,
.detail-card,
.consolidation-card {
  background: linear-gradient(170deg, rgba(23, 26, 35, 0.95), rgba(16, 18, 24, 0.98));
}

.layers-card,
.consolidation-card,
.observations-card,
.detail-card {
  padding: 1rem;
}

.consolidation-card {
  margin-top: 1rem;
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 0.75rem;
  margin-bottom: 0.75rem;
}

.card-header h2 {
  font-size: 1rem;
  margin: 0;
  letter-spacing: 0.02em;
}

.subtitle {
  margin: 0.35rem 0 0;
  opacity: 0.72;
  font-size: 0.85rem;
}

.layers-panels {
  background: transparent;
}

.layer-title-row {
  width: 100%;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.layer-name {
  font-weight: 600;
  letter-spacing: 0.02em;
}

.layer-description {
  margin: 0 0 0.75rem;
  opacity: 0.8;
}

.layer-meta p,
.consolidation-meta p,
.detail-meta p {
  margin: 0.35rem 0;
  font-size: 0.84rem;
  line-height: 1.35;
}

.filters-row {
  display: grid;
  grid-template-columns: 130px 130px minmax(200px, 1fr) 110px 140px;
  gap: 0.65rem;
  margin-bottom: 0.75rem;
}

.control-compact {
  padding-top: 0.25rem;
}

.table-wrap {
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 0.75rem;
  overflow: hidden;
}

.content-col {
  max-width: 320px;
}

.content-text {
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
  line-height: 1.3;
}

.tags-wrap {
  display: flex;
  gap: 0.3rem;
  flex-wrap: wrap;
  max-width: 220px;
}

.active-row {
  background: rgba(46, 196, 182, 0.08);
}

.pagination-row {
  display: flex;
  justify-content: flex-end;
  margin-top: 0.8rem;
}

.empty-note {
  margin: 0.6rem 0;
  opacity: 0.7;
  font-size: 0.9rem;
}

.detail-section {
  margin-bottom: 0.85rem;
}

.detail-label {
  margin: 0 0 0.45rem;
  opacity: 0.72;
  font-size: 0.8rem;
  letter-spacing: 0.06em;
  text-transform: uppercase;
}

.detail-content {
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 0.75rem;
  padding: 0.75rem;
  line-height: 1.4;
  font-size: 0.92rem;
  white-space: pre-wrap;
}

.detail-edit {
  margin-top: 0.75rem;
}

.detail-actions {
  display: flex;
  gap: 0.55rem;
  flex-wrap: wrap;
}

.consolidation-error {
  color: #ff5d73;
  font-size: 0.84rem;
  margin: 0.75rem 0;
}

@media (max-width: 1400px) {
  .memory-layout {
    grid-template-columns: minmax(260px, 300px) minmax(0, 1fr);
  }

  .detail-column {
    grid-column: 1 / -1;
  }
}

@media (max-width: 980px) {
  .memory-layout {
    grid-template-columns: 1fr;
  }

  .filters-row {
    grid-template-columns: 1fr;
  }

  .pagination-row {
    justify-content: center;
  }
}
</style>