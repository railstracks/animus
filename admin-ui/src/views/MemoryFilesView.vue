<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import { apiGet, apiRequest } from '../lib/api';

interface Agent {
  id: string;
  name: string;
  numeric_id: number;
}

interface MemoryFileListItem {
  id: number;
  source_path: string;
  file_type: string;
  content_mutable: boolean;
  agent_id: number;
  superseded: boolean;
  created_at_unix_ms: number;
  imported_at_unix_ms: number;
}

interface MemoryFileDetail extends MemoryFileListItem {
  content: string;
}

interface MemoryFileStats {
  total: number;
  by_type: Record<string, number>;
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
}

interface SearchResponse {
  results: SearchResult[];
  count: number;
}

const { t } = useI18n();

const loading = ref(false);
const loadingDetail = ref(false);
const loadingSearch = ref(false);
const loadingImport = ref(false);
const loadingBatchImport = ref(false);
const loadingUpdate = ref(false);
const deletingId = ref<number | null>(null);
const processingId = ref<number | null>(null);
const error = ref('');

const files = ref<MemoryFileListItem[]>([]);
const selectedFileId = ref<number | null>(null);
const selectedFile = ref<MemoryFileDetail | null>(null);
const stats = ref<MemoryFileStats>({ total: 0, by_type: {} });

const agents = ref<Agent[]>([]);
const selectedAgentId = ref<number>(0); // 0 = all agents

const agentFilterItems = computed(() => [
  { title: t("memoryFiles.allAgents"), value: 0 },
  ...agents.value.map(a => ({ title: a.name, value: a.numeric_id || 0 }))
]);

const agentImportItems = computed(() => [
  { title: 'Default (0)', value: 0 },
  ...agents.value.map(a => ({ title: a.name, value: a.numeric_id || 0 }))
]);

const typeFilter = ref('all');
const limit = ref(100);
const offset = ref(0);

const singleImport = ref({
  source_path: '',
  file_type: 'external_doc',
  content_mutable: true,
  agent_id: 0,
  superseded: false,
  content: ''
});
const singleFile = ref<File | null>(null);
const singleFileInputKey = ref(0);

const batchFiles = ref<File[]>([]);
const batchFileInputKey = ref(0);

const metadataForm = ref({
  source_path: '',
  file_type: 'external_doc',
  content_mutable: true,
  agent_id: 0,
  superseded: false,
  content: ''
});

const searchQuery = ref('');
const searchLimit = ref(20);
const domainObservations = ref(true);
const domainOntology = ref(true);
const domainFiles = ref(true);
const domainSessions = ref(true);
const searchResults = ref<SearchResult[]>([]);

const typeOptions = computed(() => [
  { title: t('memoryFiles.types.all'), value: 'all' },
  { title: t('memoryFiles.types.expanded_memory'), value: 'expanded_memory' },
  { title: t('memoryFiles.types.session_log'), value: 'session_log' },
  { title: t('memoryFiles.types.daily_note'), value: 'daily_note' },
  { title: t('memoryFiles.types.bootstrap_file'), value: 'bootstrap_file' },
  { title: t('memoryFiles.types.journal'), value: 'journal' },
  { title: t('memoryFiles.types.external_doc'), value: 'external_doc' }
]);

const listHeaders = computed(() => [
  { title: t('memoryFiles.list.columns.id'), key: 'id' },
  { title: t('memoryFiles.list.columns.type'), key: 'file_type' },
  { title: t('memoryFiles.list.columns.sourcePath'), key: 'source_path' },
  { title: t('memoryFiles.list.columns.contentMutable'), key: 'content_mutable' },
  { title: t('memoryFiles.list.columns.agentId'), key: 'agent_id' },
  { title: t('memoryFiles.list.columns.superseded'), key: 'superseded' },
  { title: t('memoryFiles.list.columns.status'), key: 'status' },
  { title: t('memoryFiles.list.columns.importedAt'), key: 'imported_at_unix_ms' },
  { title: t('memoryFiles.list.columns.actions'), key: 'actions', sortable: false }
]);

function displayType(value: string): string {
  const key = `memoryFiles.types.${value}`;
  const translated = t(key);
  return translated === key ? value : translated;
}

function displayDomain(value: string): string {
  const key = `memoryFiles.search.domains.${value}`;
  const translated = t(key);
  return translated === key ? value : translated;
}

function truncate(value: string, max = 220): string {
  if (value.length <= max) return value;
  return `${value.slice(0, max)}...`;
}

function formatFileSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function formatUnixMs(value: number): string {
  if (!value || Number.isNaN(value)) {
    return 'n/a';
  }
  return new Date(value).toLocaleString();
}

async function loadStats(): Promise<void> {
  const params = new URLSearchParams();
  if (selectedAgentId.value !== 0) {
    params.set('agent_id', String(selectedAgentId.value));
  }
  stats.value = await apiGet<MemoryFileStats>(`/api/v1/memory-files/stats?${params.toString()}`);
}

async function loadFiles(): Promise<void> {
  loading.value = true;
  error.value = '';
  try {
    const params = new URLSearchParams();
    if (typeFilter.value !== 'all') {
      params.set('type', typeFilter.value);
    }
    params.set('limit', String(limit.value));
    params.set('offset', String(offset.value));
    if (selectedAgentId.value !== 0) {
      params.set('agent_id', String(selectedAgentId.value));
    }
    files.value = await apiGet<MemoryFileListItem[]>(`/api/v1/memory-files?${params.toString()}`);

    if (selectedFileId.value !== null) {
      const stillPresent = files.value.some((file) => file.id === selectedFileId.value);
      if (!stillPresent) {
        selectedFileId.value = null;
        selectedFile.value = null;
      }
    }
  } catch (e) {
    error.value = e instanceof Error ? e.message : t('memoryFiles.errors.loadFiles');
    files.value = [];
  } finally {
    loading.value = false;
  }
}

async function loadFileDetail(id: number): Promise<void> {
  loadingDetail.value = true;
  error.value = '';
  try {
    const detail = await apiGet<MemoryFileDetail>(`/api/v1/memory-files/${id}`);
    selectedFile.value = detail;
    selectedFileId.value = detail.id;
    metadataForm.value = {
      source_path: detail.source_path,
      file_type: detail.file_type,
      content_mutable: detail.content_mutable,
      agent_id: detail.agent_id,
      superseded: detail.superseded,
      content: detail.content
    };
  } catch (e) {
    error.value = e instanceof Error ? e.message : t('memoryFiles.errors.loadDetail');
  } finally {
    loadingDetail.value = false;
  }
}

async function refreshAll(): Promise<void> {
  await Promise.all([loadFiles(), loadStats()]);
}

async function loadAgents(): Promise<void> {
  try {
    const data = await apiGet<{ agents: Agent[] }>('/api/v1/agents');
    agents.value = data.agents ?? [];
  } catch {
    agents.value = [];
  }
}

function onSingleFileSelected(event: Event): void {
  const input = event.target as HTMLInputElement;
  if (input.files && input.files.length > 0) {
    singleFile.value = input.files[0];
    singleImport.value.source_path = input.files[0].name;
  }
}

function clearSingleFile(): void {
  singleFile.value = null;
  singleImport.value.source_path = '';
  singleImport.value.content = '';
  singleFileInputKey.value++;
}

function displayStatus(status: number): string {
  return status === 0 ? t('memoryFiles.status.unprocessed') : t('memoryFiles.status.processed');
}

async function processFile(id: number): Promise<void> {
  try {
    await apiRequest('POST', `/api/v1/memory-files/${id}/process`);
    await loadFiles();
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to queue file';
  }
}

async function importSingleFile(): Promise<void> {
  if (!singleFile.value) {
    error.value = t('memoryFiles.errors.importRequired');
    return;
  }

  loadingImport.value = true;
  error.value = '';
  try {
    // Read file content client-side
    const content = await readFileText(singleFile.value);

    await apiRequest('POST', '/api/v1/memory-files/import', {
      source_path: singleImport.value.source_path.trim() || singleFile.value.name,
      file_type: singleImport.value.file_type,
      content_mutable: singleImport.value.content_mutable,
      agent_id: singleImport.value.agent_id,
      superseded: singleImport.value.superseded,
      content
    });

    clearSingleFile();
    singleImport.value.file_type = 'external_doc';
    singleImport.value.content_mutable = true;
    singleImport.value.agent_id = 0;
    singleImport.value.superseded = false;
    await refreshAll();
  } catch (e) {
    error.value = e instanceof Error ? e.message : t('memoryFiles.errors.importSingle');
  } finally {
    loadingImport.value = false;
  }
}

function readFileText(file: File): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result as string);
    reader.onerror = () => reject(new Error(`Failed to read file: ${file.name}`));
    reader.readAsText(file);
  });
}

function onBatchFilesSelected(event: Event): void {
  const input = event.target as HTMLInputElement;
  if (input.files) {
    batchFiles.value = Array.from(input.files);
  }
}

function removeBatchFile(index: number): void {
  batchFiles.value.splice(index, 1);
}

function clearBatchFiles(): void {
  batchFiles.value = [];
  batchFileInputKey.value++;
}

async function importBatch(): Promise<void> {
  if (batchFiles.value.length === 0) {
    error.value = t('memoryFiles.errors.batchRequired');
    return;
  }

  loadingBatchImport.value = true;
  error.value = '';
  try {
    const filesPayload = [];
    for (const file of batchFiles.value) {
      const content = await readFileText(file);
      filesPayload.push({
        source_path: file.name,
        file_type: 'external_doc',
        content_mutable: true,
        agent_id: 0,
        superseded: false,
        content
      });
    }
    await apiRequest('POST', '/api/v1/memory-files/import-batch', {
      files: filesPayload
    });
    clearBatchFiles();
    await refreshAll();
  } catch (e) {
    error.value = e instanceof Error ? e.message : t('memoryFiles.errors.importBatch');
  } finally {
    loadingBatchImport.value = false;
  }
}

async function saveMetadata(): Promise<void> {
  if (!selectedFileId.value) {
    return;
  }

  loadingUpdate.value = true;
  error.value = '';
  try {
    await apiRequest('PATCH', `/api/v1/memory-files/${selectedFileId.value}/metadata`, {
      source_path: metadataForm.value.source_path,
      file_type: metadataForm.value.file_type,
      content_mutable: metadataForm.value.content_mutable,
      agent_id: metadataForm.value.agent_id,
      superseded: metadataForm.value.superseded,
      content: metadataForm.value.content
    });
    await refreshAll();
    await loadFileDetail(selectedFileId.value);
  } catch (e) {
    error.value = e instanceof Error ? e.message : t('memoryFiles.errors.updateMetadata');
  } finally {
    loadingUpdate.value = false;
  }
}

async function deleteFile(id: number): Promise<void> {
  deletingId.value = id;
  error.value = '';
  try {
    await apiRequest('DELETE', `/api/v1/memory-files/${id}`);
    if (selectedFileId.value === id) {
      selectedFileId.value = null;
      selectedFile.value = null;
    }
    await refreshAll();
  } catch (e) {
    error.value = e instanceof Error ? e.message : t('memoryFiles.errors.delete');
  } finally {
    deletingId.value = null;
  }
}

async function runSearch(): Promise<void> {
  if (!searchQuery.value.trim()) {
    searchResults.value = [];
    return;
  }

  loadingSearch.value = true;
  error.value = '';
  try {
    const payload = await apiRequest<SearchResponse>('POST', '/api/v1/memory/search', {
      query: searchQuery.value,
      limit: searchLimit.value,
      domains: {
        observations: domainObservations.value,
        ontology: domainOntology.value,
        raw_files: domainFiles.value,
        sessions: domainSessions.value
      }
    });
    searchResults.value = payload.results ?? [];
  } catch (e) {
    error.value = e instanceof Error ? e.message : t('memoryFiles.errors.search');
    searchResults.value = [];
  } finally {
    loadingSearch.value = false;
  }
}

onMounted(() => {
  void Promise.all([refreshAll(), loadAgents()]);
});

watch(selectedAgentId, () => {
  void refreshAll();
});
</script>

<template>
  <v-row class="mb-3" align="center">
    <v-col cols="12" md="8">
      <h1 class="text-h5">{{ t('memoryFiles.title') }}</h1>
      <div class="text-medium-emphasis">{{ t('memoryFiles.subtitle') }}</div>
    </v-col>
    <v-col cols="12" md="4" class="text-md-end">
      <v-btn color="primary" prepend-icon="mdi-refresh" :loading="loading || loadingDetail" @click="refreshAll">
        {{ t('memoryFiles.actions.refresh') }}
      </v-btn>
    </v-col>
  </v-row>

  <v-alert v-if="error" type="error" variant="tonal" class="mb-4">
    {{ error }}
  </v-alert>

  <v-row class="mb-4">
    <v-col cols="12" md="3">
      <v-card variant="outlined">
        <v-card-title>{{ t('memoryFiles.stats.title') }}</v-card-title>
        <v-card-text>
          <v-select v-model="selectedAgentId"
            :items="agentFilterItems"
            item-title="title"
            item-value="value"
            :label="t('memoryFiles.fields.agentFilter')"
            density="compact"
            variant="outlined"
            class="mb-3"
          />
          <div class="text-h5 mb-3">{{ stats.total }}</div>
          <div v-for="type in typeOptions.filter((x) => x.value !== 'all')" :key="type.value" class="d-flex justify-space-between text-caption mb-1">
            <span>{{ type.title }}</span>
            <span>{{ stats.by_type?.[type.value] ?? 0 }}</span>
          </div>
        </v-card-text>
      </v-card>
    </v-col>

    <v-col cols="12" md="9">
      <v-card variant="outlined">
        <v-card-title>{{ t('memoryFiles.list.title') }}</v-card-title>
        <v-card-text>
          <v-row class="mb-2">
            <v-col cols="12" sm="5">
              <v-select
                v-model="typeFilter"
                :items="typeOptions"
                item-title="title"
                item-value="value"
                :label="t('memoryFiles.list.typeFilter')"
                density="comfortable"
                variant="outlined"
                hide-details
                @update:model-value="loadFiles"
              />
            </v-col>
            <v-col cols="6" sm="3">
              <v-text-field
                v-model.number="limit"
                type="number"
                min="1"
                max="1000"
                :label="t('memoryFiles.list.limit')"
                density="comfortable"
                variant="outlined"
                hide-details
              />
            </v-col>
            <v-col cols="6" sm="3">
              <v-text-field
                v-model.number="offset"
                type="number"
                min="0"
                :label="t('memoryFiles.list.offset')"
                density="comfortable"
                variant="outlined"
                hide-details
              />
            </v-col>
            <v-col cols="12" sm="1" class="d-flex align-center justify-end">
              <v-btn variant="tonal" icon="mdi-filter-check" @click="loadFiles" />
            </v-col>
          </v-row>

          <v-data-table
            :headers="listHeaders"
            :items="files"
            item-key="id"
            :loading="loading"
            density="comfortable"
          >
            <template #item.file_type="{ item }">
              <v-chip size="small" variant="tonal">{{ displayType(item.file_type) }}</v-chip>
            </template>
            <template #item.source_path="{ item }">
              <span class="mono">{{ item.source_path }}</span>
            </template>
            <template #item.superseded="{ item }">
              <v-chip :color="item.superseded ? 'warning' : 'success'" size="small" variant="tonal">
                {{ item.superseded ? t('memoryFiles.common.yes') : t('memoryFiles.common.no') }}
              </v-chip>
            </template>
            <template #item.content_mutable="{ item }">
              <v-chip :color="item.content_mutable ? 'primary' : 'grey'" size="small" variant="tonal">
                {{ item.content_mutable ? t('memoryFiles.common.yes') : t('memoryFiles.common.no') }}
              </v-chip>
            </template>
            <template #item.imported_at_unix_ms="{ item }">
              {{ formatUnixMs(item.imported_at_unix_ms) }}
            </template>
            <template #item.actions="{ item }">
              <v-btn size="small" variant="text" @click="loadFileDetail(item.id)">
                {{ t('memoryFiles.actions.open') }}
              </v-btn>
              <v-btn
                size="small"
                variant="text"
                color="primary"
                :loading="processingId === item.id"
                @click="processFile(item.id)"
              >
                {{ t('memoryFiles.actions.process') }}
              </v-btn>
              <v-btn
                size="small"
                variant="text"
                color="error"
                :loading="deletingId === item.id"
                @click="deleteFile(item.id)"
              >
                {{ t('memoryFiles.actions.delete') }}
              </v-btn>
            </template>
            <template #item.status="{ item }">
              <v-chip :color="item.status === 0 ? 'warning' : 'success'" size="small">
                {{ displayStatus(item.status) }}
              </v-chip>
            </template>
          </v-data-table>
        </v-card-text>
      </v-card>
    </v-col>
  </v-row>

  <v-row class="mb-4">
    <v-col cols="12" md="6">
      <v-card variant="outlined" height="100%">
        <v-card-title>{{ t('memoryFiles.import.singleTitle') }}</v-card-title>
        <v-card-text>
          <v-file-input
            :key="singleFileInputKey"
            :label="t('memoryFiles.import.selectFile')"
            density="comfortable"
            variant="outlined"
            show-size
            accept=".txt,.md,.json,.yaml,.yml,.xml,.html,.css,.js,.ts,.py,.rb,.rs,.go,.c,.cpp,.h,.hpp,.toml,.cfg,.ini,.log,.csv,.tsv"
            class="mb-2"
            @change="onSingleFileSelected"
          />
          <div v-if="singleFile" class="text-caption text-medium-emphasis mb-2">
            {{ t('memoryFiles.import.selected') }}: {{ singleFile.name }} ({{ formatFileSize(singleFile.size) }})
          </div>
          <v-row>
            <v-col cols="12" sm="6">
              <v-select
                v-model="singleImport.file_type"
                :items="typeOptions.filter((x) => x.value !== 'all')"
                item-title="title"
                item-value="value"
                :label="t('memoryFiles.fields.fileType')"
                density="comfortable"
                variant="outlined"
              />
            </v-col>
            <v-col cols="12" sm="3">
              <v-select
                v-model="singleImport.agent_id"
                :items="agentImportItems"
                item-title="title"
                item-value="value"
                :label="t('memoryFiles.fields.agentId')"
                density="comfortable"
                variant="outlined"
              />
            </v-col>
            <v-col cols="12" sm="3" class="d-flex align-center">
              <v-switch
                v-model="singleImport.content_mutable"
                :label="t('memoryFiles.fields.contentMutable')"
                color="primary"
                hide-details
              />
            </v-col>
          </v-row>
          <v-btn color="primary" prepend-icon="mdi-upload" :loading="loadingImport" :disabled="!singleFile" @click="importSingleFile">
            {{ t('memoryFiles.actions.import') }}
          </v-btn>
        </v-card-text>
      </v-card>
    </v-col>

    <v-col cols="12" md="6">
      <v-card variant="outlined" height="100%">
        <v-card-title>{{ t('memoryFiles.import.batchTitle') }}</v-card-title>
        <v-card-text>
          <v-file-input
            :key="batchFileInputKey"
            :label="t('memoryFiles.import.selectFiles')"
            density="comfortable"
            variant="outlined"
            show-size
            multiple
            counter
            accept=".txt,.md,.json,.yaml,.yml,.xml,.html,.css,.js,.ts,.py,.rb,.rs,.go,.c,.cpp,.h,.hpp,.toml,.cfg,.ini,.log,.csv,.tsv"
            class="mb-2"
            @change="onBatchFilesSelected"
          />
          <div v-if="batchFiles.length > 0" class="mb-3">
            <div class="text-caption text-medium-emphasis mb-1">
              {{ batchFiles.length }} {{ t('memoryFiles.import.filesSelected') }}
            </div>
            <v-list density="compact" class="batch-file-list">
              <v-list-item v-for="(file, idx) in batchFiles" :key="idx">
                <template #prepend>
                  <v-icon size="small">mdi-file-document-outline</v-icon>
                </template>
                <template #title>
                  <span class="text-body-2">{{ file.name }}</span>
                  <span class="text-caption text-medium-emphasis ml-2">{{ formatFileSize(file.size) }}</span>
                </template>
                <template #append>
                  <v-btn size="x-small" variant="text" color="error" icon="mdi-close" @click="removeBatchFile(idx)" />
                </template>
              </v-list-item>
            </v-list>
          </div>
          <v-btn color="primary" prepend-icon="mdi-database-import" :loading="loadingBatchImport" :disabled="batchFiles.length === 0" @click="importBatch">
            {{ t('memoryFiles.actions.importBatch') }}
          </v-btn>
        </v-card-text>
      </v-card>
    </v-col>
  </v-row>

  <v-row class="mb-4">
    <v-col cols="12" md="6">
      <v-card variant="outlined" min-height="360">
        <v-card-title>{{ t('memoryFiles.detail.title') }}</v-card-title>
        <v-card-text v-if="selectedFile">
          <v-text-field
            v-model="metadataForm.source_path"
            :label="t('memoryFiles.fields.sourcePath')"
            density="comfortable"
            variant="outlined"
            class="mb-2"
          />
          <v-row>
            <v-col cols="12" sm="6">
              <v-select
                v-model="metadataForm.file_type"
                :items="typeOptions.filter((x) => x.value !== 'all')"
                item-title="title"
                item-value="value"
                :label="t('memoryFiles.fields.fileType')"
                density="comfortable"
                variant="outlined"
              />
            </v-col>
            <v-col cols="12" sm="3">
              <v-select
                v-model="metadataForm.agent_id"
                :items="agentImportItems"
                item-title="title"
                item-value="value"
                :label="t('memoryFiles.fields.agentId')"
                density="comfortable"
                variant="outlined"
              />
            </v-col>
            <v-col cols="12" sm="3" class="d-flex align-center">
              <v-switch
                v-model="metadataForm.content_mutable"
                :label="t('memoryFiles.fields.contentMutable')"
                color="primary"
                hide-details
              />
            </v-col>
            <v-col cols="12" sm="3" class="d-flex align-center">
              <v-switch
                v-model="metadataForm.superseded"
                :label="t('memoryFiles.fields.superseded')"
                color="warning"
                hide-details
              />
            </v-col>
          </v-row>

          <div class="text-caption text-medium-emphasis mt-2">
            {{ t('memoryFiles.detail.createdAt') }}: {{ formatUnixMs(selectedFile.created_at_unix_ms) }}
          </div>
          <div class="text-caption text-medium-emphasis mb-3">
            {{ t('memoryFiles.detail.importedAt') }}: {{ formatUnixMs(selectedFile.imported_at_unix_ms) }}
          </div>

          <v-btn color="primary" prepend-icon="mdi-content-save" :loading="loadingUpdate" @click="saveMetadata">
            {{ t('memoryFiles.actions.saveMetadata') }}
          </v-btn>

          <v-divider class="my-4" />

          <h3 class="text-subtitle-1 mb-2">{{ t('memoryFiles.detail.contentTitle') }}</h3>
          <v-textarea
            v-model="metadataForm.content"
            :label="t('memoryFiles.fields.content')"
            density="comfortable"
            rows="10"
            auto-grow
            variant="outlined"
            :disabled="!metadataForm.content_mutable"
            class="mb-2"
          />
          <div v-if="!metadataForm.content_mutable" class="text-caption text-medium-emphasis">
            {{ t('memoryFiles.detail.contentImmutableNotice') }}
          </div>
        </v-card-text>
        <v-card-text v-else class="text-medium-emphasis">
          {{ t('memoryFiles.detail.empty') }}
        </v-card-text>
      </v-card>
    </v-col>

    <v-col cols="12" md="6">
      <v-card variant="outlined" min-height="360">
        <v-card-title>{{ t('memoryFiles.search.title') }}</v-card-title>
        <v-card-text>
          <v-text-field
            v-model="searchQuery"
            :label="t('memoryFiles.search.query')"
            density="comfortable"
            variant="outlined"
            class="mb-2"
            @keyup.enter="runSearch"
          />

          <v-row class="mb-2">
            <v-col cols="6" sm="3">
              <v-switch v-model="domainObservations" :label="t('memoryFiles.search.domains.observations')" hide-details color="primary" />
            </v-col>
            <v-col cols="6" sm="3">
              <v-switch v-model="domainOntology" :label="t('memoryFiles.search.domains.ontology')" hide-details color="primary" />
            </v-col>
            <v-col cols="6" sm="3">
              <v-switch v-model="domainFiles" :label="t('memoryFiles.search.domains.memory_file')" hide-details color="primary" />
            </v-col>
            <v-col cols="6" sm="3">
              <v-switch v-model="domainSessions" :label="t('memoryFiles.search.domains.sessions')" hide-details color="primary" />
            </v-col>
          </v-row>

          <v-row class="mb-2">
            <v-col cols="12" sm="4">
              <v-text-field
                v-model.number="searchLimit"
                type="number"
                min="1"
                max="200"
                :label="t('memoryFiles.search.limit')"
                density="comfortable"
                variant="outlined"
                hide-details
              />
            </v-col>
            <v-col cols="12" sm="8" class="d-flex align-center justify-end">
              <v-btn color="primary" prepend-icon="mdi-magnify" :loading="loadingSearch" @click="runSearch">
                {{ t('memoryFiles.actions.search') }}
              </v-btn>
            </v-col>
          </v-row>

          <v-list density="compact" v-if="searchResults.length > 0">
            <v-list-item v-for="result in searchResults" :key="`${result.domain}-${result.id}-${result.relevance}`">
              <template #title>
                <div class="d-flex align-center ga-2">
                  <v-chip size="x-small" variant="tonal">{{ displayDomain(result.domain) }}</v-chip>
                  <span class="text-caption text-medium-emphasis">#{{ result.id }}</span>
                  <span v-if="result.source_path" class="mono text-caption">{{ result.source_path }}</span>
                  <span v-if="result.entity_path" class="mono text-caption">{{ result.entity_path }}</span>
                  <span v-if="result.layer" class="text-caption">{{ result.layer }}</span>
                </div>
              </template>
              <template #subtitle>
                <div class="result-text">{{ truncate(result.text) }}</div>
                <div class="text-caption text-medium-emphasis mt-1">
                  {{ t('memoryFiles.search.relevance') }}: {{ result.relevance.toFixed(4) }}
                </div>
              </template>
            </v-list-item>
          </v-list>
          <div v-else class="text-medium-emphasis text-body-2">
            {{ t('memoryFiles.search.empty') }}
          </div>
        </v-card-text>
      </v-card>
    </v-col>
  </v-row>
</template>

<style scoped>
.mono {
  font-family: 'Fira Code', 'JetBrains Mono', ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
}

.content-block {
  white-space: pre-wrap;
  word-break: break-word;
  background: rgba(0, 0, 0, 0.04);
  border-radius: 8px;
  padding: 12px;
  max-height: 420px;
  overflow: auto;
}

.result-text {
  white-space: pre-wrap;
  word-break: break-word;
}

.batch-file-list {
  max-height: 240px;
  overflow-y: auto;
  background: rgba(0, 0, 0, 0.04);
  border-radius: 8px;
}
</style>
