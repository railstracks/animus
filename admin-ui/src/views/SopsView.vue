<template>
  <div class="pa-4">
    <h1 class="text-h5 mb-4">{{ t('sops.title') }}</h1>

    <!-- Server management -->
    <v-card elevation="2" rounded="lg" class="mb-4">
      <v-card-item>
        <div class="d-flex align-center justify-space-between">
          <v-card-title class="text-subtitle-1">SOP Servers</v-card-title>
          <div class="d-flex gap-2">
            <v-btn size="small" variant="text" prepend-icon="mdi-refresh" :loading="refreshing" @click="refreshSops">
              Refresh
            </v-btn>
            <v-btn size="small" variant="text" prepend-icon="mdi-cog" @click="serverDialog = true">
              Manage
            </v-btn>
          </div>
        </div>
      </v-card-item>
      <v-card-text v-if="servers.length">
        <v-chip v-for="srv in servers" :key="srv" size="small" color="primary" variant="tonal" class="mr-1 mb-1">
          {{ srv }}
        </v-chip>
      </v-card-text>
      <v-card-text v-else class="text-medium-emphasis text-body-2">
        No servers configured. Default: https://animus-sop.steadyfort.com
      </v-card-text>
    </v-card>

    <!-- Search and filters -->
    <v-card elevation="2" rounded="lg" class="mb-4">
      <v-card-text>
        <v-row align="center">
          <v-col cols="12" md="6">
            <v-text-field
              v-model="searchQuery"
              prepend-inner-icon="mdi-magnify"
              :label="t('sops.searchLabel')"
              density="comfortable"
              hide-details
              @keyup.enter="loadSops"
            />
          </v-col>
          <v-col cols="12" md="4">
            <v-select
              v-model="categoryFilter"
              :items="categories"
              :label="t('sops.categoryLabel')"
              density="comfortable"
              hide-details
              clearable
            />
          </v-col>
          <v-col cols="12" md="2">
            <v-btn color="primary" block @click="loadSops" :loading="loading">{{ t('sops.searchButton') }}</v-btn>
          </v-col>
        </v-row>
      </v-card-text>
    </v-card>

    <!-- SOP cards -->
    <v-row v-if="loading">
      <v-col cols="12" class="text-center">
        <v-progress-circular indeterminate color="primary" />
      </v-col>
    </v-row>

    <v-row v-else-if="sops.length === 0">
      <v-col cols="12" class="text-center text-medium-emphasis">
        {{ t('sops.noResults') }}
      </v-col>
    </v-row>

    <v-row v-else>
      <v-col v-for="sop in filteredSops" :key="sop.name" cols="12" md="6" lg="4">
        <v-card elevation="2" rounded="lg" class="h-100">
          <v-card-item>
            <v-card-title class="text-subtitle-1">{{ sop.title }}</v-card-title>
            <v-card-subtitle>
              <v-chip size="x-small" color="primary" class="mr-1">{{ sop.category || 'general' }}</v-chip>
              <span class="text-medium-emphasis">v{{ sop.version }}</span>
            </v-card-subtitle>
          </v-card-item>
          <v-card-text>
            <p class="text-body-2 mb-2">{{ sop.description }}</p>
            <div class="d-flex flex-wrap gap-1 mb-2">
              <v-chip v-for="tag in (sop.tags || [])" :key="tag" size="x-small" variant="outlined" class="mr-1 mb-1">
                {{ tag }}
              </v-chip>
            </div>
            <div v-if="sop.source_server" class="text-caption text-medium-emphasis">
              <v-icon size="x-small" class="mr-1">mdi-server-network</v-icon>
              {{ sop.source_server }}
            </div>
          </v-card-text>
          <v-card-actions>
            <v-btn variant="text" size="small" @click="viewSop(sop)">{{ t('sops.view') }}</v-btn>
            <v-spacer />
            <v-select
              v-model="sop._selectedAgent"
              :items="agentItems"
              item-title="name"
              item-value="id"
              density="compact"
              hide-details
              style="max-width: 160px;"
              :placeholder="t('sops.selectAgent')"
            />
            <v-btn
              color="primary"
              size="small"
              prepend-icon="mdi-download"
              :disabled="!sop._selectedAgent"
              :loading="sop._installing"
              @click="installSop(sop)"
            >
              {{ t('sops.install') }}
            </v-btn>
          </v-card-actions>
        </v-card>
      </v-col>
    </v-row>

    <!-- SOP content dialog -->
    <v-dialog v-model="viewDialog" max-width="700px">
      <v-card>
        <v-card-title class="text-h6">
          {{ viewingSop?.title }}
          <v-chip size="x-small" color="primary" class="ml-2">{{ viewingSop?.category || 'general' }}</v-chip>
        </v-card-title>
        <v-card-text>
          <pre class="text-body-2 sop-content">{{ viewingSop?.content }}</pre>
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn @click="viewDialog = false">{{ t('sops.close') }}</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Server management dialog -->
    <v-dialog v-model="serverDialog" max-width="600px">
      <v-card>
        <v-card-title class="text-h6">SOP Server Registries</v-card-title>
        <v-card-text>
          <p class="text-body-2 text-medium-emphasis mb-3">
            Configure remote SOP registry servers. Enter the base URL — the API path (/api/v1/sops) is appended automatically. The default server is always included.
          </p>
          <v-list density="comfortable">
            <v-list-item v-for="(srv, i) in editableServers" :key="i" class="px-0">
              <div class="d-flex align-center">
                <v-text-field
                  v-model="editableServers[i]"
                  density="comfortable"
                  hide-details
                  placeholder="https://example.com"
                  :rules="[v => !!v || 'URL required']"
                  prepend-icon="mdi-server-network"
                  class="flex-grow-1"
                />
                <v-tooltip text="Delete server" location="top">
                  <template #activator="{ props }">
                    <v-btn
                      v-bind="props"
                      icon="mdi-delete-outline"
                      size="small"
                      variant="text"
                      color="error"
                      class="ml-2"
                      @click="editableServers.splice(i, 1)"
                    />
                  </template>
                </v-tooltip>
              </div>
            </v-list-item>
          </v-list>
          <v-btn size="small" variant="tonal" prepend-icon="mdi-plus" class="mt-2" @click="editableServers.push('')">
            Add server
          </v-btn>
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn @click="serverDialog = false">Cancel</v-btn>
          <v-btn color="primary" :loading="savingServers" @click="saveServers">Save</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Snackbar -->
    <v-snackbar v-model="snackbar.show" :color="snackbar.color" :timeout="3000">
      {{ snackbar.text }}
    </v-snackbar>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { useI18n } from 'vue-i18n';

const { t } = useI18n();

interface Sop {
  name: string;
  title: string;
  category: string;
  version: string;
  description: string;
  tags: string[];
  source_server?: string;
  content?: string;
  _selectedAgent?: string;
  _installing?: boolean;
}

interface Agent {
  id: string;
  name: string;
}

const sops = ref<Sop[]>([]);
const loading = ref(false);
const refreshing = ref(false);
const searchQuery = ref('');
const categoryFilter = ref<string | null>(null);
const agents = ref<Agent[]>([]);
const servers = ref<string[]>([]);
const serverDialog = ref(false);
const editableServers = ref<string[]>([]);
const savingServers = ref(false);
const viewDialog = ref(false);
const viewingSop = ref<Sop | null>(null);
const snackbar = ref({ show: false, text: '', color: 'success' });

const categories = computed(() => {
  const cats = new Set(sops.value.map(s => s.category).filter(Boolean));
  return Array.from(cats).sort();
});

const agentItems = computed(() => agents.value);

const filteredSops = computed(() => {
  if (!categoryFilter.value) return sops.value;
  return sops.value.filter(s => s.category === categoryFilter.value);
});

async function loadSops() {
  loading.value = true;
  try {
    let url = '/api/v1/sops';
    const params: string[] = [];
    if (searchQuery.value) params.push(`q=${encodeURIComponent(searchQuery.value)}`);
    if (categoryFilter.value) params.push(`category=${encodeURIComponent(categoryFilter.value)}`);
    if (params.length) url += '?' + params.join('&');

    const resp = await fetch(url);
    if (!resp.ok) throw new Error('Failed to load SOPs');
    const data = await resp.json();
    sops.value = (data.sops || []).map((s: Sop) => ({ ...s, _selectedAgent: undefined, _installing: false }));
  } catch (e) {
    console.error('Failed to load SOPs:', e);
  } finally {
    loading.value = false;
  }
}

async function loadServers() {
  try {
    const resp = await fetch('/api/v1/sops/servers');
    if (!resp.ok) return;
    const data = await resp.json();
    servers.value = data.servers || [];
  } catch (e) {
    console.error('Failed to load SOP servers:', e);
  }
}

async function loadAgents() {
  try {
    const resp = await fetch('/api/v1/agents');
    if (!resp.ok) return;
    const data = await resp.json();
    const list = data.agents || data;
    agents.value = (Array.isArray(list) ? list : []).map((a: any) => ({
      id: a.numeric_id ?? a.id,
      name: a.name || a.id,
    }));
  } catch (e) {
    console.error('Failed to load agents:', e);
  }
}

async function viewSop(sop: Sop) {
  try {
    const resp = await fetch(`/api/v1/sops/${sop.name}`);
    if (!resp.ok) throw new Error('Failed to load SOP');
    const data = await resp.json();
    viewingSop.value = { ...sop, content: data.content, source_server: data.source_server };
    viewDialog.value = true;
  } catch (e) {
    console.error(e);
  }
}

async function installSop(sop: Sop) {
  if (!sop._selectedAgent) return;
  sop._installing = true;
  try {
    const resp = await fetch(`/api/v1/sops/${sop.name}/install`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ agent_id: sop._selectedAgent }),
    });
    if (!resp.ok) {
      const err = await resp.json().catch(() => ({}));
      throw new Error(err.error || 'Install failed');
    }
    const data = await resp.json();
    snackbar.value = {
      show: true,
      text: t('sops.installSuccess', { title: sop.title, id: data.memory_file_id }),
      color: 'success',
    };
  } catch (e: any) {
    snackbar.value = {
      show: true,
      text: e.message || t('sops.installFailed'),
      color: 'error',
    };
  } finally {
    sop._installing = false;
  }
}

async function refreshSops() {
  refreshing.value = true;
  try {
    const resp = await fetch('/api/v1/sops/refresh', { method: 'POST' });
    if (!resp.ok) throw new Error('Refresh failed');
    const data = await resp.json();
    snackbar.value = {
      show: true,
      text: `Refreshed: ${data.total_sops} SOPs from ${data.servers} server(s)`,
      color: 'success',
    };
    await loadSops();
  } catch (e: any) {
    snackbar.value = {
      show: true,
      text: e.message || 'Refresh failed',
      color: 'error',
    };
  } finally {
    refreshing.value = false;
  }
}

function openServerDialog() {
  editableServers.value = [...servers.value];
  serverDialog.value = true;
}

async function saveServers() {
  savingServers.value = true;
  try {
    const resp = await fetch('/api/v1/sops/servers', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ servers: editableServers.value.filter(s => s.trim()) }),
    });
    if (!resp.ok) {
      const err = await resp.json().catch(() => ({}));
      throw new Error(err.error || 'Save failed');
    }
    const data = await resp.json();
    servers.value = data.servers || [];
    serverDialog.value = false;
    snackbar.value = {
      show: true,
      text: data.warning || 'Servers updated. Restart kernel to fetch from new servers.',
      color: 'success',
    };
  } catch (e: any) {
    snackbar.value = {
      show: true,
      text: e.message || 'Save failed',
      color: 'error',
    };
  } finally {
    savingServers.value = false;
  }
}

onMounted(() => {
  loadSops();
  loadAgents();
  loadServers();
});
</script>

<style scoped>
.sop-content {
  white-space: pre-wrap;
  word-wrap: break-word;
  font-family: monospace;
  font-size: 0.875rem;
  max-height: 60vh;
  overflow-y: auto;
}
.gap-1 { gap: 4px; }
.gap-2 { gap: 8px; }
</style>