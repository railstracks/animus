<template>
  <div class="pa-4">
    <h1 class="text-h5 mb-4">Diffusion Providers</h1>

    <v-card elevation="2" rounded="lg">
      <v-data-table
        :headers="headers"
        :items="providers"
        :loading="loading"
        density="comfortable"
      >
        <template v-slot:top>
          <v-toolbar flat>
            <v-toolbar-title class="text-subtitle-1">Providers</v-toolbar-title>
            <v-spacer />
            <v-btn color="primary" prepend-icon="mdi-plus" @click="openCreate">
              New Provider
            </v-btn>
          </v-toolbar>
        </template>

        <template v-slot:item.api_key="{ value }">
          <span class="text-medium-emphasis">{{ '•'.repeat(8) }}</span>
        </template>

        <template v-slot:item.actions="{ item }">
          <v-btn icon="mdi-refresh" variant="text" size="small" @click="refreshModels(item)" title="List models" :loading="item._loading" />
          <v-btn icon="mdi-pencil" variant="text" size="small" @click="openEdit(item)" />
          <v-btn icon="mdi-delete" variant="text" size="small" color="error" @click="confirmDelete(item)" />
        </template>
      </v-data-table>
    </v-card>

    <!-- Models display -->
    <v-card v-if="models.length" elevation="2" rounded="lg" class="mt-4">
      <v-toolbar flat>
        <v-toolbar-title class="text-subtitle-1">
          Models for {{ modelsProvider }}
        </v-toolbar-title>
        <v-spacer />
        <v-btn icon="mdi-close" variant="text" size="small" @click="models = []" />
      </v-toolbar>
      <v-data-table
        :headers="modelHeaders"
        :items="models"
        density="comfortable"
        hide-default-header
      >
        <template v-slot:item.type="{ value }">
          <v-chip size="x-small" :color="value === 'video' ? 'purple' : 'blue'">
            {{ value }}
          </v-chip>
        </template>
      </v-data-table>
    </v-card>

    <!-- Create/Edit Dialog -->
    <v-dialog v-model="dialog" max-width="560">
      <v-card>
        <v-card-title>{{ editing ? 'Edit Provider' : 'Create Provider' }}</v-card-title>
        <v-card-text>
          <v-text-field v-model="dialogForm.id" label="Provider ID" density="comfortable" :disabled="editing" class="mb-3" hint="e.g. 'getimg'" />
          <v-select v-model="dialogForm.type" :items="['getimg']" label="Type" density="comfortable" class="mb-3" @update:model-value="onTypeChange" />
          <v-text-field v-model="dialogForm.base_url" label="Base URL" density="comfortable" class="mb-3" hint="e.g. https://api.getimg.ai" />
          <v-text-field v-model="dialogForm.api_key" label="API Key" type="password" density="comfortable" class="mb-3" />
          <v-text-field v-model="dialogForm.default_model" label="Default Model" density="comfortable" class="mb-3" hint="Optional fallback model ID" />
          <v-text-field v-model="dialogForm.default_aspect_ratio" label="Default Aspect Ratio" density="comfortable" hint="e.g. 1:1" />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn text @click="dialog = false">Cancel</v-btn>
          <v-btn color="primary" @click="saveProvider" :loading="saving">{{ editing ? 'Save' : 'Create' }}</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Delete Confirmation -->
    <v-dialog v-model="deleteDialog" max-width="400">
      <v-card>
        <v-card-title>Delete Provider</v-card-title>
        <v-card-text>Are you sure you want to delete "{{ deleteTarget?.id }}"?</v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn text @click="deleteDialog = false">Cancel</v-btn>
          <v-btn color="error" @click="doDelete">Delete</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <v-snackbar v-model="snackbar" :color="snackbarColor" :timeout="3000">
      {{ snackbarText }}
    </v-snackbar>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { useAuthStore } from '../stores/auth';

const auth = useAuthStore();

const providers = ref<any[]>([]);
const loading = ref(false);
const dialog = ref(false);
const editing = ref(false);
const editingId = ref('');
const saving = ref(false);
const deleteDialog = ref(false);
const deleteTarget = ref<any>(null);
const snackbar = ref(false);
const snackbarText = ref('');
const snackbarColor = ref('success');
const models = ref<any[]>([]);
const modelsProvider = ref('');

const dialogForm = ref({
  id: '', type: 'getimg', base_url: '', api_key: '', default_model: '', default_aspect_ratio: ''
});

const headers = [
  { title: 'ID', key: 'id' },
  { title: 'Type', key: 'type' },
  { title: 'Base URL', key: 'base_url' },
  { title: 'API Key', key: 'api_key' },
  { title: 'Default Model', key: 'default_model' },
  { title: 'Actions', key: 'actions', sortable: false },
];

const modelHeaders = [
  { title: 'Model ID', key: 'id' },
  { title: 'Name', key: 'name' },
  { title: 'Type', key: 'type' },
];

async function loadProviders() {
  loading.value = true;
  try {
    const resp = await fetch('/api/v1/diffusion/providers');
    providers.value = await resp.json();
  } finally {
    loading.value = false;
  }
}

function openCreate() {
  editing.value = false;
  dialogForm.value = { id: '', type: 'getimg', base_url: 'https://api.getimg.ai', api_key: '', default_model: '', default_aspect_ratio: '1:1' };
  dialogForm.value.id = nextProviderId('getimg');
  dialog.value = true;
}

function nextProviderId(type: string): string {
  let n = 1;
  while (providers.value.some(p => p.id === `${type}-${n}`)) {
    n++;
  }
  return `${type}-${n}`;
}

function onTypeChange() {
  if (dialogForm.value.type === 'getimg') {
    if (!dialogForm.value.base_url) {
      dialogForm.value.base_url = 'https://api.getimg.ai';
    }
    if (!editing.value) {
      dialogForm.value.id = nextProviderId('getimg');
    }
    if (!dialogForm.value.default_aspect_ratio) {
      dialogForm.value.default_aspect_ratio = '1:1';
    }
  }
}

function openEdit(provider: any) {
  editing.value = true;
  editingId.value = provider.id;
  dialogForm.value = { ...provider };
  dialog.value = true;
}

async function saveProvider() {
  saving.value = true;
  try {
    if (editing.value) {
      const resp = await fetch(`/api/v1/diffusion/providers/${editingId.value}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(dialogForm.value),
      });
      const data = await resp.json();
      if (!resp.ok) throw new Error(data.error || 'Failed to update');
      showSnackbar('Provider updated');
    } else {
      const resp = await fetch('/api/v1/diffusion/providers', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(dialogForm.value),
      });
      const data = await resp.json();
      if (!resp.ok) throw new Error(data.error || 'Failed to create');
      showSnackbar('Provider created');
    }
    dialog.value = false;
    await loadProviders();
  } catch (e: any) {
    showSnackbar(e.message, 'error');
  } finally {
    saving.value = false;
  }
}

function confirmDelete(provider: any) {
  deleteTarget.value = provider;
  deleteDialog.value = true;
}

async function doDelete() {
  if (!deleteTarget.value) return;
  try {
    const resp = await fetch(`/api/v1/diffusion/providers/${deleteTarget.value.id}`, {
      method: 'DELETE',
    });
    if (!resp.ok) throw new Error('Failed to delete');
    showSnackbar('Provider deleted');
    deleteDialog.value = false;
    await loadProviders();
  } catch (e: any) {
    showSnackbar(e.message, 'error');
  }
}

async function refreshModels(provider: any) {
  try {
    provider._loading = true;
    const resp = await fetch(`/api/v1/diffusion/providers/${provider.id}/models`);
    const data = await resp.json();
    if (resp.ok) {
      models.value = data;
      modelsProvider.value = provider.id;
    } else {
      throw new Error(data.error || 'Failed to list models');
    }
  } catch (e: any) {
    showSnackbar(e.message, 'error');
  } finally {
    provider._loading = false;
  }
}

function showSnackbar(text: string, color = 'success') {
  snackbarText.value = text;
  snackbarColor.value = color;
  snackbar.value = true;
}

onMounted(loadProviders);
</script>
