<template>
  <div class="pa-4">
    <h1 class="text-h5 mb-4">{{ t('packages.title') }}</h1>

    <!-- Installed packages -->
    <v-card elevation="2" rounded="lg" class="mb-4">
      <v-card-item>
        <div class="d-flex align-center justify-space-between">
          <v-card-title class="text-subtitle-1">{{ t('packages.installed') }}</v-card-title>
          <div class="d-flex gap-2">
            <v-btn size="small" variant="text" prepend-icon="mdi-refresh" :loading="loading" @click="loadPackages">
              {{ t('packages.refresh') }}
            </v-btn>
            <v-btn size="small" variant="text" prepend-icon="mdi-package-down" @click="installDialog = true">
              {{ t('packages.installFromManifest') }}
            </v-btn>
            <v-btn size="small" variant="text" prepend-icon="mdi-cloud-download-outline" @click="registryDialog = true">
              {{ t('packages.installFromRegistry') }}
            </v-btn>
          </div>
        </div>
      </v-card-item>
      <v-card-text v-if="!loading && packages.length === 0" class="text-medium-emphasis text-body-2">
        {{ t('packages.noneInstalled') }}
      </v-card-text>
      <v-card-text v-else-if="loading" class="text-center">
        <v-progress-circular indeterminate color="primary" />
      </v-card-text>
      <v-list v-else lines="two">
        <v-list-item
          v-for="p in packages"
          :key="p.id"
          :title="p.display_name || p.name"
          :subtitle="p.description"
          @click="openDetail(p.id)"
        >
          <template #prepend>
            <v-icon :color="p.enabled ? 'success' : 'grey'">mdi-package-variant-closed</v-icon>
          </template>
          <template #append>
            <div class="d-flex align-center gap-2" @click.stop>
              <v-chip size="x-small" variant="outlined" class="mr-1">v{{ p.version }}</v-chip>
              <v-chip v-if="p.registry_source" size="x-small" color="primary" variant="tonal" class="mr-1">
                <v-icon start size="x-small">mdi-cloud-download-outline</v-icon>
                {{ p.registry_source }}
              </v-chip>
              <v-chip v-if="p.locally_modified" size="x-small" color="warning" variant="tonal" class="mr-1">
                {{ t('packages.modified') }}
              </v-chip>
              <v-switch
                :model-value="p.enabled"
                color="success"
                density="compact"
                hide-details
                :loading="p._toggling"
                @update:model-value="toggleEnabled(p, $event)"
              />
            </div>
          </template>
        </v-list-item>
      </v-list>
    </v-card>

    <!-- Detail dialog (tabbed) -->
    <PackageDetailDialog
      :model-value="dialogOpen"
      :pkg-id="selectedId"
      @update:model-value="dialogOpen = $event"
      @changed="loadPackages"
    />

    <!-- Install dialog -->
    <v-dialog v-model="installDialog" max-width="720">
      <v-card rounded="lg">
        <v-card-item>
          <v-card-title class="text-subtitle-1">{{ t('packages.installFromManifest') }}</v-card-title>
        </v-card-item>
        <v-card-text>
          <p class="text-body-2 text-medium-emphasis mb-2">{{ t('packages.installHint') }}</p>
          <v-textarea
            v-model="manifestText"
            rows="12"
            :label="'manifest v1 (JSON)'"
            mono
            :error-messages="manifestError"
          />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="installDialog = false">{{ t('packages.cancel') }}</v-btn>
          <v-btn color="primary" :loading="installing" :disabled="!manifestText.trim()" @click="install">
            {{ t('packages.install') }}
          </v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Install from registry dialog -->
    <v-dialog v-model="registryDialog" max-width="560">
      <v-card rounded="lg">
        <v-card-item>
          <v-card-title class="text-subtitle-1">{{ t('packages.installFromRegistry') }}</v-card-title>
        </v-card-item>
        <v-card-text>
          <v-text-field
            v-model="registryUrl"
            :label="t('packages.registryLabel')"
            density="comfortable"
            :hint="t('packages.registryHint')"
            persistent-hint
            class="mb-2"
          />
          <v-text-field
            v-model="registryName"
            :label="t('packages.nameLabel')"
            density="comfortable"
            class="mb-2"
          />
          <v-text-field
            v-model="registryVersion"
            :label="t('packages.versionLabel')"
            density="comfortable"
            :hint="t('packages.versionHint')"
            persistent-hint
          />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="registryDialog = false">{{ t('packages.cancel') }}</v-btn>
          <v-btn color="primary" :loading="registryInstalling" :disabled="!registryUrl.trim() || !registryName.trim()" @click="installFromRegistry">
            {{ t('packages.install') }}
          </v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <v-snackbar v-model="snackbar.show" :color="snackbar.color" timeout="4000">
      {{ snackbar.text }}
    </v-snackbar>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import PackageDetailDialog from '../components/packages/PackageDetailDialog.vue';
import type { PackageRow } from '../components/packages/types';

const { t } = useI18n();

const packages = ref<PackageRow[]>([]);
const loading = ref(false);
const selectedId = ref<string | null>(null);
const dialogOpen = ref(false);
const installDialog = ref(false);
const manifestText = ref('');
const manifestError = ref('');
const installing = ref(false);
const registryDialog = ref(false);
const registryUrl = ref('https://animus-registry.steadyfort.com');
const registryName = ref('');
const registryVersion = ref('');
const registryInstalling = ref(false);
const snackbar = ref({ show: false, text: '', color: 'success' });

function toast(text: string, color = 'success') {
  snackbar.value = { show: true, text, color };
}

async function loadPackages() {
  loading.value = true;
  try {
    const resp = await fetch('/api/v1/api/packages');
    if (!resp.ok) throw new Error('load failed');
    const data = await resp.json();
    packages.value = (data.packages || []).map((p: PackageRow) => ({ ...p, _toggling: false }));
  } catch (e) {
    console.error('Failed to load packages:', e);
    toast(t('packages.loadFailed'), 'error');
  } finally {
    loading.value = false;
  }
}

function openDetail(id: string) {
  selectedId.value = id;
  dialogOpen.value = true;
}

async function toggleEnabled(p: PackageRow, enabled: boolean) {
  p._toggling = true;
  try {
    const resp = await fetch(`/api/v1/api/packages/${p.id}/enabled`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled }),
    });
    if (!resp.ok) throw new Error('toggle failed');
    p.enabled = enabled;
  } catch (e) {
    console.error(e);
    toast(t('packages.toggleFailed'), 'error');
  } finally {
    p._toggling = false;
  }
}

async function install() {
  manifestError.value = '';
  let parsed: any;
  try {
    parsed = JSON.parse(manifestText.value);
  } catch {
    manifestError.value = t('packages.invalidJson');
    return;
  }
  installing.value = true;
  try {
    const resp = await fetch('/api/v1/api/packages', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(parsed),
    });
    if (!resp.ok) {
      const err = await resp.json().catch(() => ({}));
      throw new Error(err.error || 'install failed');
    }
    const data = await resp.json();
    toast(t('packages.installSuccess', { name: data.name }));
    installDialog.value = false;
    manifestText.value = '';
    await loadPackages();
  } catch (e: any) {
    toast(e.message || t('packages.installFailed'), 'error');
  } finally {
    installing.value = false;
  }
}

async function installFromRegistry() {
  registryInstalling.value = true;
  try {
    const resp = await fetch('/api/v1/api/packages/install-from-registry', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        registry: registryUrl.value,
        name: registryName.value,
        version: registryVersion.value || undefined,
      }),
    });
    if (!resp.ok) {
      const err = await resp.json().catch(() => ({}));
      throw new Error(err.error || 'install failed');
    }
    const data = await resp.json();
    toast(t('packages.registryInstallSuccess', {
      name: data.name,
      hash: (data.content_hash || '').slice(0, 12),
    }));
    registryDialog.value = false;
    registryName.value = '';
    registryVersion.value = '';
    await loadPackages();
  } catch (e: any) {
    toast(e.message || t('packages.installFailed'), 'error');
  } finally {
    registryInstalling.value = false;
  }
}

loadPackages();
</script>
