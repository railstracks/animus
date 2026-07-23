<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';

import { apiGet, apiRequest } from '../lib/api';

// --- Types matching actual API responses ---

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

interface DiaryMetadata {
  exists: boolean;
  entry_count: number;
  last_entry_date: number | null;
}

// --- State ---

const { t } = useI18n();

const agents = ref<AgentEntity[]>([]);
const selectedAgentId = ref<string>('');
const metadata = ref<DiaryMetadata | null>(null);
const loadingAgents = ref(false);
const loadingMeta = ref(false);
const error = ref('');

// Export
const exporting = ref(false);

// Import
const importStrategy = ref<'merge' | 'replace'>('merge');
const importFile = ref<File | null>(null);
const importing = ref(false);
const showImportDialog = ref(false);
const importResult = ref<{ imported: number; skipped: number } | null>(null);

// --- Helpers ---

function formatTimestamp(unixMs: number | null): string {
  if (unixMs === null || unixMs === 0) return '—';
  try {
    return new Date(unixMs).toLocaleString();
  } catch {
    return '—';
  }
}

// --- Data ---

async function loadAgents() {
  loadingAgents.value = true;
  error.value = '';
  try {
    const data = await apiGet<AgentsResponse>('/api/v1/agents');
    agents.value = data.agents || [];
    if (agents.value.length > 0 && !selectedAgentId.value) {
      selectedAgentId.value = agents.value[0].id;
      await loadMetadata();
    }
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load agents';
  } finally {
    loadingAgents.value = false;
  }
}

async function loadMetadata() {
  if (!selectedAgentId.value) {
    metadata.value = null;
    return;
  }
  loadingMeta.value = true;
  error.value = '';
  try {
    metadata.value = await apiGet<DiaryMetadata>(`/api/v1/diary/${encodeURIComponent(selectedAgentId.value)}/metadata`);
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load diary metadata';
    metadata.value = null;
  } finally {
    loadingMeta.value = false;
  }
}

function onAgentChange() {
  loadMetadata();
}

// --- Export ---

async function doExport() {
  if (!selectedAgentId.value) return;
  exporting.value = true;
  error.value = '';
  try {
    const response = await fetch(
      `${resolveApiBaseUrl()}/api/v1/diary/${encodeURIComponent(selectedAgentId.value)}/export`,
      {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${readAdminToken()}`
        },
        body: JSON.stringify({})
      }
    );

    if (!response.ok) {
      const text = await response.text();
      throw new Error(`Export failed: ${text}`);
    }

    const blob = await response.blob();
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `diary-${selectedAgentId.value}.aidiary`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Export failed';
  } finally {
    exporting.value = false;
  }
}

// --- Import ---

function openImportDialog() {
  importStrategy.value = 'merge';
  importFile.value = null;
  importResult.value = null;
  showImportDialog.value = true;
}

function onFileSelect(event: Event) {
  const target = event.target as HTMLInputElement;
  if (target.files && target.files.length > 0) {
    importFile.value = target.files[0];
  }
}

async function doImport() {
  if (!selectedAgentId.value || !importFile.value) return;
  importing.value = true;
  error.value = '';
  importResult.value = null;
  try {
    const reader = new FileReader();
    const base64 = await new Promise<string>((resolve, reject) => {
      reader.onload = () => {
        const result = reader.result as string;
        const base64Data = result.split(',')[1] || result;
        resolve(base64Data);
      };
      reader.onerror = () => reject(new Error('Failed to read file'));
      reader.readAsDataURL(importFile.value!);
    });

    const result = await apiRequest<{ imported: number; skipped: number }>(
      'POST',
      `/api/v1/diary/${encodeURIComponent(selectedAgentId.value)}/import`,
      {
        data: base64,
        merge_strategy: importStrategy.value
      }
    );

    importResult.value = { imported: result.imported, skipped: result.skipped };
    await loadMetadata();
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Import failed';
  } finally {
    importing.value = false;
  }
}

function resolveApiBaseUrl(): string {
  const fromEnv = import.meta.env.VITE_API_BASE_URL as string | undefined;
  if (fromEnv && fromEnv.trim().length > 0) {
    return fromEnv.replace(/\/$/, '');
  }
  if (typeof window !== 'undefined' && window.location?.origin) {
    return window.location.origin;
  }
  return 'http://127.0.0.1:8080';
}

function readAdminToken(): string {
  if (typeof localStorage === 'undefined') return '';
  return localStorage.getItem('animus…oken') ?? '';
}

const agentItems = computed(() =>
  agents.value.map(a => ({ title: a.name || a.id, value: a.id }))
);

onMounted(loadAgents);
</script>

<template>
  <div class="diary-layout">
    <v-card rounded="xl" class="diary-card">
      <div class="diary-header">
        <h2>{{ t('sidebar.links.diary') }}</h2>
        <v-btn
          size="small"
          variant="text"
          :loading="loadingMeta"
          :disabled="!selectedAgentId"
          @click="loadMetadata"
        >
          Refresh
        </v-btn>
      </div>

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

      <!-- Agent selector -->
      <div class="agent-selector">
        <v-select
          v-model="selectedAgentId"
          :items="agentItems"
          label="Agent"
          density="comfortable"
          variant="outlined"
          @update:model-value="onAgentChange"
        />
      </div>

      <div v-if="loadingAgents" class="loading-state">Loading agents...</div>
      <div v-else-if="agents.length === 0" class="empty-state">
        <p>No agents configured. Create an agent first.</p>
      </div>
      <div v-else-if="loadingMeta && !metadata" class="loading-state">Loading metadata...</div>

      <!-- Metadata display -->
      <div v-if="metadata" class="metadata-panel">
        <div class="meta-row">
          <div class="meta-icon">
            <v-icon :color="metadata.exists ? 'success' : 'grey'">
              {{ metadata.exists ? 'mdi-notebook-check' : 'mdi-notebook-off-outline' }}
            </v-icon>
          </div>
          <div class="meta-content">
            <span class="meta-label">Diary Active</span>
            <span class="meta-value">{{ metadata.exists ? 'Yes' : 'No entries yet' }}</span>
          </div>
        </div>

        <div class="meta-row">
          <div class="meta-icon">
            <v-icon color="info">mdi-counter</v-icon>
          </div>
          <div class="meta-content">
            <span class="meta-label">Entry Count</span>
            <span class="meta-value">{{ metadata.entry_count }}</span>
          </div>
        </div>

        <div class="meta-row">
          <div class="meta-icon">
            <v-icon color="warning">mdi-clock-outline</v-icon>
          </div>
          <div class="meta-content">
            <span class="meta-label">Most Recent Entry</span>
            <span class="meta-value">{{ formatTimestamp(metadata.last_entry_date) }}</span>
          </div>
        </div>

        <!-- Export / Import actions -->
        <div class="actions-row">
          <v-btn
            size="small"
            color="primary"
            variant="tonal"
            prepend-icon="mdi-download"
            :loading="exporting"
            :disabled="!metadata.exists"
            @click="doExport"
          >
            Export
          </v-btn>
          <v-btn
            size="small"
            color="primary"
            variant="tonal"
            prepend-icon="mdi-upload"
            @click="openImportDialog"
          >
            Import
          </v-btn>
        </div>

        <div class="privacy-note">
          <v-icon size="16" color="grey">mdi-shield-lock-outline</v-icon>
          <span>Diary content is private to the agent. The encryption key is maintained by the agent model — admin never has access. Exported .aidiary files can only be imported back by the same agent.</span>
        </div>
      </div>
    </v-card>

    <!-- Import Dialog -->
    <v-dialog v-model="showImportDialog" max-width="480px">
      <v-card rounded="xl">
        <v-card-title>Import Diary</v-card-title>
        <v-card-text>
          <p class="dialog-hint">
            Import a .aidiary backup for this agent. The file is encrypted with the agent's own key — only backups from this same agent can be imported.
          </p>
          <v-file-input
            label=".aidiary file"
            accept=".aidiary"
            density="comfortable"
            variant="outlined"
            @change="onFileSelect"
          />
          <v-select
            v-model="importStrategy"
            :items="[{ title: 'Merge (keep existing, add new)', value: 'merge' }, { title: 'Replace (delete all, then import)', value: 'replace' }]"
            label="Merge strategy"
            density="comfortable"
            variant="outlined"
          />

          <v-alert v-if="importResult" type="success" variant="tonal" density="compact" class="mt-3">
            Imported {{ importResult.imported }} entries{{ importResult.skipped > 0 ? `, skipped ${importResult.skipped}` : '' }}.
          </v-alert>
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="showImportDialog = false">Cancel</v-btn>
          <v-btn color="primary" :loading="importing" :disabled="!importFile" @click="doImport">Import</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </div>
</template>

<style scoped>
.diary-layout {
  max-width: 640px;
  margin: 0 auto;
}

.diary-card {
  padding: 1.5rem;
  background: linear-gradient(170deg, rgba(23, 26, 35, 0.95), rgba(16, 18, 24, 0.98));
}

.diary-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 1.25rem;
}

.diary-header h2 {
  font-size: 1rem;
  margin: 0;
}

.agent-selector {
  margin-bottom: 1.5rem;
}

.loading-state,
.empty-state {
  text-align: center;
  opacity: 0.7;
  padding: 2rem 1rem;
}

.metadata-panel {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.meta-row {
  display: flex;
  align-items: center;
  gap: 1rem;
  border: 1px solid rgba(var(--v-theme-on-surface), 0.08);
  border-radius: 12px;
  padding: 1rem;
}

.meta-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 40px;
  height: 40px;
  border-radius: 10px;
  background: rgba(var(--v-theme-on-surface), 0.04);
}

.meta-content {
  display: flex;
  flex-direction: column;
  gap: 0.2rem;
}

.meta-label {
  font-size: 0.72rem;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  opacity: 0.6;
}

.meta-value {
  font-size: 0.95rem;
  font-weight: 500;
}

.actions-row {
  display: flex;
  gap: 0.75rem;
  margin-top: 0.5rem;
}

.privacy-note {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  margin-top: 0.75rem;
  padding: 0.75rem 1rem;
  border-radius: 8px;
  background: rgba(var(--v-theme-on-surface), 0.03);
  font-size: 0.78rem;
  opacity: 0.6;
}

.dialog-hint {
  font-size: 0.85rem;
  opacity: 0.75;
  margin-bottom: 1rem;
  line-height: 1.5;
}
</style>