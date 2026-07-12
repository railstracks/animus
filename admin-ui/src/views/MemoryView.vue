<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';

import { apiGet, apiRequest } from '../lib/api';

// --- Types matching our actual API ---

interface AgentEntity {
  id: string;
  name: string;
  description: string;
  identity: string;
  avatar: string;
  model: { provider: string; model_id: string };
  temperature: number;
  reasoning: { enabled: boolean; effort: string };
  budget: { max_output_tokens: number; max_context_tokens: number };
}

interface AgentsResponse {
  agents: AgentEntity[];
}

interface MemoryLayer {
  id: number;
  name: string;
  horizon: string;
  sort_order: number;
  evaluation_interval_seconds: number;
  cron_expr: string;
  consolidation_review_prompt: string;
  consolidation_intake_prompt: string;
  intake_interval: string | null;
  token_budget: number;
  enabled: boolean;
  created_at_unix_ms: number;
  updated_at_unix_ms: number;
  observation_count: number;
  has_perspective: boolean;
}

interface Observation {
  id: number;
  layer_id: number;
  text: string;
  weight: number;
  decay_rate: number;
  tags: string;
  source: string;
  created_at_unix_ms: number;
  last_evaluated_at_ms: number;
  next_review_at_ms: number;
}

interface Perspective {
  id: number;
  layer_id: number;
  retrospective: string;
  retrospective_valence: string;
  current_perspective: string;
  current_valence: string;
  future_perspective: string;
  future_valence: string;
  updated_at_unix_ms: number;
}

// --- State ---

const { t, locale } = useI18n();

const agents = ref<AgentEntity[]>([]);
const selectedAgentId = ref<string>('');
const loadingAgents = ref(false);

const layers = ref<MemoryLayer[]>([]);
const loading = ref(false);
const error = ref('');

// Layer editing
const showLayerDialog = ref(false);
const editingLayer = ref<MemoryLayer | null>(null);
const layerForm = ref({
  name: '',
  horizon: '1 day',
  sort_order: 0,
  evaluation_interval_seconds: 86400,
  cron_expr: '0 * * * *',
  consolidation_review_prompt: '',
  consolidation_intake_prompt: '',
  intake_enabled: false,
  intake_interval: '0 * * * *',
  token_budget: 4096,
  enabled: false
});
const savingLayer = ref(false);
const loadingDefaultReviewPrompt = ref(false);
const loadingDefaultIntakePrompt = ref(false);

// Observations
const selectedLayerId = ref<number | null>(null);
const observations = ref<Observation[]>([]);
const loadingObs = ref(false);
const showObsDialog = ref(false);
const obsForm = ref({ text: '', weight: 1.0, decay_rate: 0.95, tags: '[]', source: 'manual' });
const savingObs = ref(false);

// Perspectives
const perspective = ref<Perspective | null>(null);
const loadingPersp = ref(false);
const showPerspDialog = ref(false);
const perspForm = ref({
  retrospective: '', retrospective_valence: '',
  current_perspective: '', current_valence: '',
  future_perspective: '', future_valence: ''
});
const savingPersp = ref(false);

// Delete confirmation
const deletingId = ref<number | null>(null);
const deletingType = ref<'layer' | 'observation'>('layer');

// --- Helpers ---

function formatDuration(seconds: number): string {
  if (seconds < 60) return `${seconds}s`;
  if (seconds < 3600) return `${(seconds / 60).toFixed(0)}m`;
  if (seconds < 86400) return `${(seconds / 3600).toFixed(0)}h`;
  if (seconds < 604800) return `${(seconds / 86400).toFixed(0)}d`;
  if (seconds < 2592000) return `${(seconds / 604800).toFixed(0)}w`;
  return `${(seconds / 2592000).toFixed(0)}mo`;
}

function formatDate(unixMs: number): string {
  return new Date(unixMs).toLocaleString();
}

// --- Layer CRUD ---

async function loadAgents() {
  loadingAgents.value = true;
  try {
    const data = await apiGet<AgentsResponse>('/api/v1/agents');
    agents.value = data.agents || [];
  } catch {
    // Agents list is optional — don't block on failure
  } finally {
    loadingAgents.value = false;
  }
}

async function loadLayers() {
  loading.value = true;
  error.value = '';
  try {
    const params = new URLSearchParams();
    if (selectedAgentId.value) params.set('agent_id', selectedAgentId.value);
    const qs = params.toString();
    const url = '/api/v1/memory/layers' + (qs ? '?' + qs : '');
    layers.value = await apiGet<MemoryLayer[]>(url);
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load layers';
  } finally {
    loading.value = false;
  }
}

function openCreateLayer() {
  editingLayer.value = null;
  layerForm.value = {
    name: '',
    horizon: '1 day',
    sort_order: layers.value.length,
    evaluation_interval_seconds: 86400,
    cron_expr: '0 * * * *',
    consolidation_review_prompt: '',
    consolidation_intake_prompt: '',
    intake_enabled: false,
    intake_interval: '0 * * * *',
    token_budget: 4096,
    enabled: false
  };
  showLayerDialog.value = true;
}

function openEditLayer(layer: MemoryLayer) {
  editingLayer.value = layer;
  layerForm.value = {
    name: layer.name,
    horizon: layer.horizon,
    sort_order: layer.sort_order,
    evaluation_interval_seconds: layer.evaluation_interval_seconds,
    cron_expr: layer.cron_expr,
    consolidation_review_prompt: layer.consolidation_review_prompt,
    consolidation_intake_prompt: layer.consolidation_intake_prompt ?? '',
    intake_enabled: typeof layer.intake_interval === 'string' && layer.intake_interval.length > 0,
    intake_interval: layer.intake_interval ?? '0 * * * *',
    token_budget: layer.token_budget,
    enabled: layer.enabled
  };
  showLayerDialog.value = true;
}

function normalizeLocaleForTemplateRequest(value: string): string {
  if (value === 'koKR') return 'ko-KR';
  if (value === 'koKP') return 'ko-KP';
  return value;
}

async function loadDefaultPrompt(kind: 'review' | 'intake') {
  const selectedLocale = normalizeLocaleForTemplateRequest(String(locale.value || 'en'));
  if (kind === 'review') loadingDefaultReviewPrompt.value = true;
  if (kind === 'intake') loadingDefaultIntakePrompt.value = true;
  try {
    const payload = await apiGet<{ content: string }>(
      `/api/v1/memory/templates/${kind}?locale=${encodeURIComponent(selectedLocale)}`
    );
    if (kind === 'review') {
      layerForm.value.consolidation_review_prompt = payload.content ?? '';
    } else {
      layerForm.value.consolidation_intake_prompt = payload.content ?? '';
    }
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load default prompt';
  } finally {
    if (kind === 'review') loadingDefaultReviewPrompt.value = false;
    if (kind === 'intake') loadingDefaultIntakePrompt.value = false;
  }
}

async function saveLayer() {
  savingLayer.value = true;
  try {
    const payload = {
      agent_id: selectedAgentId.value,
      name: layerForm.value.name,
      horizon: layerForm.value.horizon,
      sort_order: layerForm.value.sort_order,
      evaluation_interval_seconds: layerForm.value.evaluation_interval_seconds,
      cron_expr: layerForm.value.cron_expr,
      consolidation_review_prompt: layerForm.value.consolidation_review_prompt,
      consolidation_intake_prompt: layerForm.value.intake_enabled
        ? layerForm.value.consolidation_intake_prompt
        : '',
      intake_interval: layerForm.value.intake_enabled
        ? (layerForm.value.intake_interval || '0 * * * *')
        : null,
      token_budget: layerForm.value.token_budget,
      enabled: layerForm.value.enabled
    };
    if (editingLayer.value) {
      await apiRequest('PUT', `/api/v1/memory/layers/${editingLayer.value.id}`, payload);
    } else {
      await apiRequest('POST', '/api/v1/memory/layers', payload);
    }
    showLayerDialog.value = false;
    await loadLayers();
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to save layer';
  } finally {
    savingLayer.value = false;
  }
}

async function deleteLayer(id: number) {
  try {
    await apiRequest('DELETE', `/api/v1/memory/layers/${id}`);
    if (selectedLayerId.value === id) selectedLayerId.value = null;
    deletingId.value = null;
    await loadLayers();
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to delete layer';
  }
}

// --- Observations ---

async function selectLayer(layerId: number) {
  selectedLayerId.value = layerId;
  await Promise.all([loadObservations(), loadPerspective()]);
}

async function loadObservations() {
  if (!selectedLayerId.value) { observations.value = []; return; }
  loadingObs.value = true;
  try {
    const payload = await apiGet<{ observations?: Observation[] } | Observation[]>(`/api/v1/memory/layers/${selectedLayerId.value}/observations`);
    observations.value = Array.isArray(payload) ? payload : (payload.observations ?? []);
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load observations';
  } finally {
    loadingObs.value = false;
  }
}

function openCreateObs() {
  obsForm.value = { text: '', weight: 1.0, decay_rate: 0.95, tags: '[]', source: 'manual' };
  showObsDialog.value = true;
}

async function saveObs() {
  if (!selectedLayerId.value) return;
  savingObs.value = true;
  try {
    await apiRequest('POST', `/api/v1/memory/layers/${selectedLayerId.value}/observations`, obsForm.value);
    showObsDialog.value = false;
    await Promise.all([loadObservations(), loadLayers()]);
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to save observation';
  } finally {
    savingObs.value = false;
  }
}

// Consolidation triggers
const consolidating = ref(false);
const intaking = ref(false);
const consolidateResult = ref<string | null>(null);
const intakeResult = ref<string | null>(null);

async function triggerConsolidate() {
  if (!selectedLayerId.value || consolidating.value) return;
  consolidating.value = true;
  consolidateResult.value = null;
  try {
    const payload = await apiRequest('POST', `/api/v1/memory/layers/${selectedLayerId.value}/consolidate`);
    consolidateResult.value = payload.triggered ? `Consolidation review triggered for ${payload.layer}` : `Failed: ${payload.error ?? 'unknown error'}`;
  } catch (e) {
    consolidateResult.value = `Error: ${e instanceof Error ? e.message : 'unknown'}`;
  } finally {
    consolidating.value = false;
    setTimeout(() => { consolidateResult.value = null; }, 5000);
  }
}

async function triggerIntake() {
  if (!selectedAgentId.value || intaking.value) return;
  intaking.value = true;
  intakeResult.value = null;
  try {
    const payload = await apiRequest('POST', `/api/v1/memory/agents/${selectedAgentId.value}/intake`);
    intakeResult.value = payload.triggered ? `Intake triggered for agent` : `Failed: ${payload.error ?? 'unknown error'}`;
  } catch (e) {
    intakeResult.value = `Error: ${e instanceof Error ? e.message : 'unknown'}`;
  } finally {
    intaking.value = false;
    setTimeout(() => { intakeResult.value = null; }, 5000);
  }
}

async function deleteObs(id: number) {
  try {
    await apiRequest('DELETE', `/api/v1/memory/observations/${id}`);
    deletingId.value = null;
    await Promise.all([loadObservations(), loadLayers()]);
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to delete observation';
  }
}

// --- Perspectives ---

async function loadPerspective() {
  if (!selectedLayerId.value) { perspective.value = null; return; }
  loadingPersp.value = true;
  try {
    perspective.value = await apiGet<Perspective>(`/api/v1/memory/layers/${selectedLayerId.value}/perspective`);
  } catch {
    perspective.value = null;
  } finally {
    loadingPersp.value = false;
  }
}

function openEditPersp() {
  if (!perspective.value) return;
  perspForm.value = {
    retrospective: perspective.value.retrospective,
    retrospective_valence: perspective.value.retrospective_valence,
    current_perspective: perspective.value.current_perspective,
    current_valence: perspective.value.current_valence,
    future_perspective: perspective.value.future_perspective,
    future_valence: perspective.value.future_valence
  };
  showPerspDialog.value = true;
}

async function savePersp() {
  if (!selectedLayerId.value) return;
  savingPersp.value = true;
  try {
    await apiRequest('PUT', `/api/v1/memory/layers/${selectedLayerId.value}/perspective`, perspForm.value);
    showPerspDialog.value = false;
    await loadPerspective();
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to save perspective';
  } finally {
    savingPersp.value = false;
  }
}

const selectedLayer = computed(() => layers.value.find(l => l.id === selectedLayerId.value));

onMounted(async () => {
  await loadAgents();
  if (agents.value.length > 0) {
    selectedAgentId.value = agents.value[0].id;
  }
  await loadLayers();
});
</script>

<template>
  <div class="memory-layout">
    <!-- Layers List -->
    <section class="layers-panel">
      <v-card rounded="xl" class="panel-card">
        <v-select
          v-model="selectedAgentId"
          :items="agents.map(a => ({ value: a.id, title: a.name }))"
"
          density="compact"
          variant="outlined"
          hide-details
          label="Agent"
          @update:model-value="loadLayers"
        />
        <div class="panel-header">
          <h2>Memory Layers</h2>
          <div class="header-actions">
            <v-btn size="small" variant="text" :loading="loading" @click="loadLayers">Refresh</v-btn>
            <v-btn size="small" color="primary" variant="tonal" @click="openCreateLayer">+ New Layer</v-btn>
          </div>
        </div>

        <v-alert v-if="error" type="error" variant="tonal" density="compact" closable class="mb-3" @click:close="error = ''">
          {{ error }}
        </v-alert>

        <div v-if="loading" class="loading-state">Loading...</div>

        <div v-else-if="layers.length === 0" class="empty-state">
          <p>No memory layers yet. Create one to get started.</p>
        </div>

        <div v-else class="layers-list">
          <div
            v-for="layer in layers"
            :key="layer.id"
            class="layer-card"
            :class="{ active: selectedLayerId === layer.id }"
            @click="selectLayer(layer.id)"
          >
            <div class="layer-header">
              <span class="layer-name">{{ layer.name }}</span>
              <div class="layer-badges">
                <v-chip size="x-small" variant="tonal" :color="layer.enabled ? 'success' : 'grey'">{{ layer.enabled ? 'Active' : 'Disabled' }}</v-chip>
                <v-chip size="x-small" variant="tonal" color="info">{{ layer.horizon }}</v-chip>
                <v-chip size="x-small" variant="tonal">{{ layer.observation_count }} obs</v-chip>
              </div>
            </div>
            <div class="layer-details">
              <span>Schedule: {{ layer.cron_expr }}</span>
              <span>Review: {{ layer.cron_expr || 'disabled' }}</span>
              <span>Review: {{ formatDuration(layer.evaluation_interval_seconds) }}</span>
              <span>Budget: {{ layer.token_budget }} tokens</span>
            </div>
            <div class="layer-actions">
              <v-btn size="x-small" variant="text" @click.stop="openEditLayer(layer)">Edit</v-btn>
              <v-btn size="x-small" variant="text" color="error" @click.stop="deletingId = layer.id; deletingType = 'layer'">Delete</v-btn>
            </div>
          </div>
        </div>
      </v-card>
    </section>

    <!-- Observations + Perspectives -->
    <section class="content-panel">
      <v-card rounded="xl" class="panel-card">
        <template v-if="!selectedLayer">
          <div class="empty-state">
            <p>Select a layer to view observations and perspectives.</p>
          </div>
        </template>

        <template v-else>
          <div class="panel-header">
            <div>
              <h2>{{ selectedLayer.name }}</h2>
              <p class="subtitle">{{ selectedLayer.horizon }} horizon &middot; {{ selectedLayer.observation_count }} observations</p>
            </div>
            <div class="panel-actions">
              <v-btn size="small" color="primary" variant="tonal" @click="openCreateObs">+ Add Observation</v-btn>
              <v-btn size="small" color="success" variant="tonal" :loading="consolidating" @click="triggerConsolidate">Review Now</v-btn>
              <v-btn v-if="selectedAgentId" size="small" color="info" variant="tonal" :loading="intaking" @click="triggerIntake">Perform Intake</v-btn>
            </div>
          </div>
          <div v-if="consolidateResult || intakeResult" class="trigger-status">
            <span v-if="consolidateResult" class="status-msg success">{{ consolidateResult }}</span>
            <span v-if="intakeResult" class="status-msg info">{{ intakeResult }}</span>
          </div>

          <!-- Perspectives -->
          <div class="persp-section" v-if="perspective">
            <div class="persp-header">
              <h3>Perspectives</h3>
              <v-btn size="x-small" variant="text" @click="openEditPersp">Edit</v-btn>
            </div>
            <div class="persp-grid">
              <div class="persp-card retrospective">
                <div class="persp-label">Retrospective</div>
                <div class="persp-text">{{ perspective.retrospective || '—' }}</div>
                <div v-if="perspective.retrospective_valence" class="persp-valence">{{ perspective.retrospective_valence }}</div>
              </div>
              <div class="persp-card current">
                <div class="persp-label">Current Posture</div>
                <div class="persp-text">{{ perspective.current_perspective || '—' }}</div>
                <div v-if="perspective.current_valence" class="persp-valence">{{ perspective.current_valence }}</div>
              </div>
              <div class="persp-card future">
                <div class="persp-label">Future Outlook</div>
                <div class="persp-text">{{ perspective.future_perspective || '—' }}</div>
                <div v-if="perspective.future_valence" class="persp-valence">{{ perspective.future_valence }}</div>
              </div>
            </div>
          </div>

          <!-- Observations -->
          <div class="obs-section">
            <div class="obs-header">
              <h3>Observations</h3>
              <v-btn size="small" variant="text" :loading="loadingObs" @click="loadObservations">Refresh</v-btn>
            </div>

            <div v-if="loadingObs" class="loading-state">Loading...</div>
            <div v-else-if="observations.length === 0" class="empty-note">No observations in this layer.</div>
            <div v-else class="obs-list">
              <div v-for="obs in observations" :key="obs.id" class="obs-item">
                <div class="obs-content">
                  <span class="obs-text">{{ obs.text }}</span>
                  <span class="obs-meta">
                    {{ formatDate(obs.created_at_unix_ms) }} &middot; weight {{ obs.weight.toFixed(2) }} &middot; {{ obs.source }}
                    <template v-if="obs.next_review_at_ms > 0">&middot; next review {{ formatDate(obs.next_review_at_ms) }}</template>
                  </span>
                </div>
                <v-btn size="x-small" variant="text" color="error" @click="deletingId = obs.id; deletingType = 'observation'">
                  Delete
                </v-btn>
              </div>
            </div>
          </div>
        </template>
      </v-card>
    </section>

    <!-- Layer Create/Edit Dialog -->
    <v-dialog v-model="showLayerDialog" max-width="520px">
      <v-card rounded="xl">
        <v-card-title>{{ editingLayer ? 'Edit Layer' : 'Create Layer' }}</v-card-title>
        <v-card-text>
          <v-text-field v-model="layerForm.name" label="Name" density="comfortable" variant="outlined" />
          <v-text-field v-model="layerForm.horizon" label="Horizon" density="comfortable" variant="outlined" hint="Semantic timeframe label (for AI guidance)" persistent-hint />
          <v-text-field v-model.number="layerForm.sort_order" label="Sort order" type="number" density="comfortable" variant="outlined" />
          <v-text-field v-model.number="layerForm.evaluation_interval_seconds" label="Evaluation interval (seconds)" type="number" density="comfortable" variant="outlined" />
          <v-text-field v-model="layerForm.cron_expr" label="Consolidation schedule (cron)" density="comfortable" variant="outlined" />
          <div class="d-flex justify-space-between align-center mb-1">
            <div class="text-caption">Consolidation review prompt</div>
            <v-btn
              size="x-small"
              variant="text"
              :loading="loadingDefaultReviewPrompt"
              @click="loadDefaultPrompt('review')"
            >
              Load Default Prompt
            </v-btn>
          </div>
          <v-textarea
            v-model="layerForm.consolidation_review_prompt"
            label="Consolidation review prompt"
            rows="3"
            density="comfortable"
            variant="outlined"
          />
          <v-text-field v-model.number="layerForm.token_budget" label="Token budget" type="number" density="comfortable" variant="outlined" />
          <v-checkbox v-model="layerForm.enabled" label="Enabled" density="comfortable" hint="Enable consolidation for this layer" persistent-hint />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="showLayerDialog = false">Cancel</v-btn>
          <v-btn color="primary" :loading="savingLayer" @click="saveLayer">{{ editingLayer ? 'Save' : 'Create' }}</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Observation Create Dialog -->
    <v-dialog v-model="showObsDialog" max-width="520px">
      <v-card rounded="xl">
        <v-card-title>Add Observation</v-card-title>
        <v-card-text>
          <v-textarea v-model="obsForm.text" label="Observation text" rows="3" density="comfortable" variant="outlined" />
          <v-text-field v-model.number="obsForm.weight" label="Weight" type="number" step="0.01" density="comfortable" variant="outlined" />
          <v-text-field v-model.number="obsForm.decay_rate" label="Decay rate" type="number" step="0.01" density="comfortable" variant="outlined" />
          <v-text-field v-model="obsForm.source" label="Source" density="comfortable" variant="outlined" />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="showObsDialog = false">Cancel</v-btn>
          <v-btn color="primary" :loading="savingObs" @click="saveObs">Add</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Perspective Edit Dialog -->
    <v-dialog v-model="showPerspDialog" max-width="600px">
      <v-card rounded="xl">
        <v-card-title>Edit Perspectives</v-card-title>
        <v-card-text>
          <v-textarea v-model="perspForm.retrospective" label="Retrospective" rows="2" density="comfortable" variant="outlined" />
          <v-text-field v-model="perspForm.retrospective_valence" label="Retrospective valence" density="comfortable" variant="outlined" />
          <v-textarea v-model="perspForm.current_perspective" label="Current perspective" rows="2" density="comfortable" variant="outlined" />
          <v-text-field v-model="perspForm.current_valence" label="Current valence" density="comfortable" variant="outlined" />
          <v-textarea v-model="perspForm.future_perspective" label="Future perspective" rows="2" density="comfortable" variant="outlined" />
          <v-text-field v-model="perspForm.future_valence" label="Future valence" density="comfortable" variant="outlined" />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="showPerspDialog = false">Cancel</v-btn>
          <v-btn color="primary" :loading="savingPersp" @click="savePersp">Save</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Delete Confirmation Dialog -->
    <v-dialog v-model="deletingId" max-width="400px">
      <v-card rounded="xl">
        <v-card-title>Confirm Delete</v-card-title>
        <v-card-text>
          <p v-if="deletingType === 'layer'">Delete this layer and all its observations? This cannot be undone.</p>
          <p v-else>Delete this observation? This cannot be undone.</p>
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="deletingId = null">Cancel</v-btn>
          <v-btn color="error" @click="deletingType === 'layer' ? deleteLayer(deletingId!) : deleteObs(deletingId!)">Delete</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </div>
</template>

<style scoped>
.memory-layout {
  display: grid;
  grid-template-columns: 360px minmax(0, 1fr);
  gap: 1rem;
  min-height: calc(100vh - 160px);
}

.panel-card {
  padding: 1rem;
  background: linear-gradient(170deg, rgba(23, 26, 35, 0.95), rgba(16, 18, 24, 0.98));
}

.panel-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 0.75rem;
  margin-bottom: 1rem;
}

.panel-header h2 {
  font-size: 1rem;
  margin: 0;
}

.panel-actions {
  display: flex;
  gap: 0.5rem;
  align-items: center;
  flex-wrap: wrap;
}

.trigger-status {
  margin-bottom: 0.75rem;
  display: flex;
  gap: 1rem;
}

.status-msg {
  font-size: 0.82rem;
  padding: 0.25rem 0.5rem;
  border-radius: 4px;
}

.status-msg.success {
  color: #4caf50;
  background: rgba(76, 175, 80, 0.1);
}

.status-msg.info {
  color: #2196f3;
  background: rgba(33, 150, 243, 0.1);
}

.header-actions {
  display: flex;
  gap: 0.5rem;
}

.subtitle {
  margin: 0.25rem 0 0;
  opacity: 0.7;
  font-size: 0.82rem;
}

.loading-state, .empty-state, .empty-note {
  text-align: center;
  opacity: 0.7;
  padding: 2rem 1rem;
}

/* Layers list */
.layers-list {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.layer-card {
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 12px;
  padding: 0.75rem;
  cursor: pointer;
  transition: border-color 0.15s, background 0.15s;
}

.layer-card:hover {
  border-color: rgba(255, 255, 255, 0.15);
}

.layer-card.active {
  border-color: rgba(46, 196, 182, 0.4);
  background: rgba(46, 196, 182, 0.06);
}

.layer-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0.4rem;
}

.layer-name {
  font-weight: 600;
  font-size: 0.92rem;
}

.layer-badges {
  display: flex;
  gap: 0.35rem;
}

.layer-details {
  display: flex;
  gap: 1rem;
  font-size: 0.78rem;
  opacity: 0.7;
  margin-bottom: 0.4rem;
}

.layer-actions {
  display: flex;
  gap: 0.3rem;
}

/* Perspectives */
.persp-section {
  margin-bottom: 1.5rem;
}

.persp-header, .obs-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0.75rem;
}

.persp-header h3, .obs-header h3 {
  font-size: 0.9rem;
  margin: 0;
  letter-spacing: 0.02em;
}

.persp-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 0.75rem;
}

.persp-card {
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 10px;
  padding: 0.75rem;
}

.persp-label {
  font-size: 0.7rem;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  opacity: 0.6;
  margin-bottom: 0.4rem;
}

.persp-text {
  font-size: 0.85rem;
  line-height: 1.4;
  white-space: pre-wrap;
  max-height: 80px;
  overflow-y: auto;
}

.persp-valence {
  margin-top: 0.4rem;
  font-size: 0.75rem;
  opacity: 0.6;
  font-style: italic;
}

.persp-card.retrospective { border-left: 3px solid rgba(255, 159, 28, 0.5); }
.persp-card.current { border-left: 3px solid rgba(46, 196, 182, 0.5); }
.persp-card.future { border-left: 3px solid rgba(58, 134, 255, 0.5); }

/* Observations */
.obs-list {
  display: flex;
  flex-direction: column;
  gap: 0.4rem;
}

.obs-item {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 0.75rem;
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 8px;
  padding: 0.6rem 0.75rem;
}

.obs-content {
  flex: 1;
  min-width: 0;
}

.obs-text {
  display: block;
  line-height: 1.35;
  font-size: 0.88rem;
}

.obs-meta {
  display: block;
  font-size: 0.72rem;
  opacity: 0.5;
  margin-top: 0.25rem;
}

@media (max-width: 900px) {
  .memory-layout {
    grid-template-columns: 1fr;
  }
  .persp-grid {
    grid-template-columns: 1fr;
  }
}
</style>
