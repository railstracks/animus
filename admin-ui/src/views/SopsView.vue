<template>
  <div class="pa-4">
    <h1 class="text-h5 mb-4">Standard Operating Procedures</h1>

    <!-- Search and filters -->
    <v-card elevation="2" rounded="lg" class="mb-4">
      <v-card-text>
        <v-row align="center">
          <v-col cols="12" md="6">
            <v-text-field
              v-model="searchQuery"
              prepend-inner-icon="mdi-magnify"
              label="Search SOPs"
              density="comfortable"
              hide-details
              @keyup.enter="loadSops"
            />
          </v-col>
          <v-col cols="12" md="4">
            <v-select
              v-model="categoryFilter"
              :items="categories"
              label="Category"
              density="comfortable"
              hide-details
              clearable
            />
          </v-col>
          <v-col cols="12" md="2">
            <v-btn color="primary" block @click="loadSops" :loading="loading">Search</v-btn>
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
        No SOPs found.
      </v-col>
    </v-row>

    <v-row v-else>
      <v-col v-for="sop in filteredSops" :key="sop.name" cols="12" md="6" lg="4">
        <v-card elevation="2" rounded="lg" class="h-100">
          <v-card-item>
            <v-card-title class="text-subtitle-1">{{ sop.title }}</v-card-title>
            <v-card-subtitle>
              <v-chip size="x-small" color="primary" class="mr-1">{{ sop.category }}</v-chip>
              <span class="text-medium-emphasis">v{{ sop.version }}</span>
            </v-card-subtitle>
          </v-card-item>
          <v-card-text>
            <p class="text-body-2 mb-2">{{ sop.description }}</p>
            <div v-if="sop.tags && sop.tags.length" class="d-flex flex-wrap gap-1">
              <v-chip v-for="tag in sop.tags" :key="tag" size="x-small" variant="outlined" class="mr-1 mb-1">
                {{ tag }}
              </v-chip>
            </div>
          </v-card-text>
          <v-card-actions>
            <v-btn variant="text" size="small" @click="viewSop(sop)">View</v-btn>
            <v-spacer />
            <v-select
              v-model="sop._selectedAgent"
              :items="agentItems"
              item-title="name"
              item-value="id"
              density="compact"
              hide-details
              style="max-width: 160px;"
              placeholder="Select agent"
            />
            <v-btn
              color="primary"
              size="small"
              prepend-icon="mdi-download"
              :disabled="!sop._selectedAgent"
              :loading="sop._installing"
              @click="installSop(sop)"
            >
              Install
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
          <v-chip size="x-small" color="primary" class="ml-2">{{ viewingSop?.category }}</v-chip>
        </v-card-title>
        <v-card-text>
          <pre class="text-body-2 sop-content">{{ viewingSop?.content }}</pre>
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn @click="viewDialog = false">Close</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Install result snackbar -->
    <v-snackbar v-model="snackbar.show" :color="snackbar.color" :timeout="3000">
      {{ snackbar.text }}
    </v-snackbar>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';

interface Sop {
  name: string;
  title: string;
  category: string;
  version: string;
  description: string;
  tags: string[];
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
const searchQuery = ref('');
const categoryFilter = ref<string | null>(null);
const agents = ref<Agent[]>([]);
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

async function loadAgents() {
  try {
    const resp = await fetch('/api/v1/agents');
    if (!resp.ok) return;
    const data = await resp.json();
    const list = data.agents || data;
    agents.value = (Array.isArray(list) ? list : []).map((a: any) => ({
      id: a.id || a.agent_id,
      name: a.name || a.id || a.agent_id,
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
    viewingSop.value = { ...sop, content: data.content };
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
      text: `Installed "${sop.title}" to agent memory (file ID: ${data.memory_file_id})`,
      color: 'success',
    };
  } catch (e: any) {
    snackbar.value = {
      show: true,
      text: e.message || 'Failed to install SOP',
      color: 'error',
    };
  } finally {
    sop._installing = false;
  }
}

onMounted(() => {
  loadSops();
  loadAgents();
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
</style>
