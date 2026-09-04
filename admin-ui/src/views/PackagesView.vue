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

    <!-- Detail dialog -->
    <v-dialog v-model="detailDialog" max-width="900" scrollable>
      <v-card v-if="detail" rounded="lg">
        <v-card-item>
          <v-card-title class="text-subtitle-1 d-flex align-center">
            {{ detail.display_name || detail.name }}
            <v-chip size="x-small" variant="outlined" class="ml-3">v{{ detail.version }}</v-chip>
            <v-chip v-if="detail.enabled" size="x-small" color="success" variant="tonal" class="ml-2">
              {{ t('packages.enabled') }}
            </v-chip>
            <v-chip v-else size="x-small" color="grey" variant="tonal" class="ml-2">
              {{ t('packages.disabled') }}
            </v-chip>
          </v-card-title>
          <v-card-subtitle>{{ detail.description }}</v-card-subtitle>
        </v-card-item>

        <v-divider />

        <v-card-text style="max-height: 60vh;">
          <!-- State editor -->
          <h3 class="text-subtitle-2 mb-2">{{ t('packages.state') }}</h3>
          <p v-if="stateFields.length === 0" class="text-caption text-medium-emphasis mb-4">
            {{ t('packages.noState') }}
          </p>
          <v-row v-else class="mb-4">
            <v-col v-for="f in stateFields" :key="f.key" cols="12" md="6">
              <v-text-field
                v-model="f.value"
                :label="f.key + (f.secret ? ' 🔒' : '')"
                :type="f.secret && !f.reveal ? 'password' : 'text'"
                :hint="f.secret ? t('packages.secretHint') : (f.default ? 'default: ' + f.default : '')"
                persistent-hint
                density="comfortable"
                :append-inner-icon="f.secret ? (f.reveal ? 'mdi-eye-off' : 'mdi-eye') : undefined"
                @click:append-inner="f.reveal = !f.reveal"
              />
            </v-col>
            <v-col cols="12">
              <v-btn size="small" color="primary" variant="tonal" :loading="savingState" @click="saveState">
                {{ t('packages.saveState') }}
              </v-btn>
            </v-col>
          </v-row>

          <!-- Commands -->
          <h3 class="text-subtitle-2 mb-2">{{ t('packages.commands') }}</h3>
          <p v-if="!detail.commands || detail.commands.length === 0" class="text-caption text-medium-emphasis mb-4">
            {{ t('packages.noCommands') }}
          </p>
          <v-expansion-panels v-else class="mb-4" variant="accordion">
            <v-expansion-panel v-for="c in detail.commands" :key="c.id">
              <v-expansion-panel-title>
                <v-chip size="x-small" :color="c.kind === 'hook' ? 'deep-purple' : 'primary'" variant="tonal" class="mr-2">
                  {{ c.kind }}
                </v-chip>
                {{ c.name }}
                <span v-if="c.event" class="ml-2 text-caption text-medium-emphasis">on {{ c.event }}</span>
              </v-expansion-panel-title>
              <v-expansion-panel-text>
                <p class="text-body-2 mb-2">{{ c.description }}</p>
                <pre v-if="c.parameters" class="text-caption mb-2">{{ c.parameters }}</pre>
                <pre class="text-caption" style="max-height: 220px; overflow: auto;">{{ c.script }}</pre>
              </v-expansion-panel-text>
            </v-expansion-panel>
          </v-expansion-panels>

          <!-- Connections -->
          <h3 class="text-subtitle-2 mb-2">{{ t('packages.connections') }}</h3>
          <p v-if="!detail.connections || detail.connections.length === 0" class="text-caption text-medium-emphasis mb-4">
            {{ t('packages.noConnections') }}
          </p>
          <v-table v-else density="compact" class="mb-4">
            <thead>
              <tr><th>{{ t('packages.name') }}</th><th>{{ t('packages.type') }}</th><th>Poll</th><th>{{ t('packages.hooks') }}</th></tr>
            </thead>
            <tbody>
              <tr v-for="c in detail.connections" :key="c.id">
                <td>{{ c.name }}</td>
                <td>{{ c.type }}</td>
                <td>{{ c.poll ? c.poll + 's' : '—' }}</td>
                <td class="text-caption">{{ c.hooks }}</td>
              </tr>
            </tbody>
          </v-table>

          <!-- Execute console (debug) -->
          <h3 class="text-subtitle-2 mb-2">{{ t('packages.executeTitle') }}</h3>
          <v-row>
            <v-col cols="12" md="4">
              <v-select
                v-model="execCommand"
                :items="actionCommands"
                item-title="name"
                item-value="name"
                :label="t('packages.command')"
                density="comfortable"
                hide-details
              />
            </v-col>
            <v-col cols="12" md="8">
              <v-textarea
                v-model="execArgs"
                :label="t('packages.argsLabel')"
                rows="2"
                auto-grow
                density="comfortable"
                hide-details
                placeholder='{ "symbol": "AAPL" }'
              />
            </v-col>
            <v-col cols="12">
              <v-btn size="small" color="primary" :loading="executing" :disabled="!execCommand" @click="runExecute">
                <v-icon start size="small">mdi-play</v-icon>
                {{ t('packages.execute') }}
              </v-btn>
            </v-col>
          </v-row>
          <pre
            v-if="execResult !== null"
            class="mt-2 pa-3 rounded"
            :class="execResult && execResult.success === false ? 'exec-error' : 'exec-ok'"
            style="max-height: 260px; overflow: auto;"
          >{{ JSON.stringify(execResult, null, 2) }}</pre>
        </v-card-text>

        <v-divider />
        <v-card-actions>
          <v-btn color="error" variant="text" :loading="uninstalling" @click="uninstall">
            <v-icon start>mdi-delete-outline</v-icon>
            {{ t('packages.uninstall') }}
          </v-btn>
          <v-spacer />
          <v-btn variant="text" @click="detailDialog = false">{{ t('packages.close') }}</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

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
import { ref, computed } from 'vue';
import { useI18n } from 'vue-i18n';

const { t } = useI18n();

interface PackageRow {
  id: string;
  name: string;
  display_name: string;
  description: string;
  version: string;
  registry_source: string | null;
  registry_version: string | null;
  locally_modified: boolean;
  enabled: boolean;
  keywords: string[];
  _toggling?: boolean;
}

interface CommandRow {
  id: string;
  name: string;
  kind: string;
  event: string | null;
  description: string;
  parameters: string | null;
  script: string | null;
}

interface ConnectionRow {
  id: string;
  name: string;
  type: string;
  enabled: boolean;
  poll: number | null;
  hooks: string | null;
}

interface PackageDetail extends PackageRow {
  state_schema: string;
  state: Record<string, any>;
  commands: CommandRow[];
  connections: ConnectionRow[];
}

interface StateField {
  key: string;
  value: string;
  secret: boolean;
  default: string | null;
  reveal: boolean;
}

const packages = ref<PackageRow[]>([]);
const loading = ref(false);
const detail = ref<PackageDetail | null>(null);
const detailDialog = ref(false);
const installDialog = ref(false);
const manifestText = ref('');
const manifestError = ref('');
const installing = ref(false);
const registryDialog = ref(false);
const registryUrl = ref('https://animus-registry.steadyfort.com');
const registryName = ref('');
const registryVersion = ref('');
const registryInstalling = ref(false);
const savingState = ref(false);
const uninstalling = ref(false);
const executing = ref(false);
const execCommand = ref<string | null>(null);
const execArgs = ref('');
const execResult = ref<any>(null);
const stateFields = ref<StateField[]>([]);
const snackbar = ref({ show: false, text: '', color: 'success' });

const actionCommands = computed(() =>
  (detail.value?.commands || []).filter(c => c.kind === 'action')
);

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

async function openDetail(id: string) {
  try {
    const resp = await fetch(`/api/v1/api/packages/${id}`);
    if (!resp.ok) throw new Error('detail failed');
    detail.value = await resp.json();
    buildStateFields();
    execCommand.value = null;
    execArgs.value = '';
    execResult.value = null;
    detailDialog.value = true;
  } catch (e) {
    console.error(e);
    toast(t('packages.loadFailed'), 'error');
  }
}

function buildStateFields() {
  const fields: StateField[] = [];
  if (detail.value) {
    let schema: Record<string, any> = {};
    try { schema = JSON.parse(detail.value.state_schema || '{}'); } catch { /* ignore */ }
    const state = detail.value.state || {};
    for (const key of Object.keys(schema)) {
      const def = schema[key] || {};
      const isSecret = !!def.secret;
      const current = state[key];
      // Secrets come back masked ("***") — show placeholder, not the mask
      fields.push({
        key,
        value: isSecret ? '' : (current === undefined || current === null ? '' : String(current)),
        secret: isSecret,
        default: def.default ?? null,
        reveal: false,
      });
    }
  }
  stateFields.value = fields;
}

async function saveState() {
  if (!detail.value) return;
  savingState.value = true;
  try {
    const next: Record<string, any> = {};
    for (const f of stateFields.value) {
      if (f.secret && f.value === '') continue; // untouched secret = keep existing
      next[f.key] = f.value;
    }
    const resp = await fetch(`/api/v1/api/packages/${detail.value.id}/state`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ state: next }),
    });
    if (!resp.ok) {
      const err = await resp.json().catch(() => ({}));
      throw new Error(err.error || 'save failed');
    }
    toast(t('packages.stateSaved'));
    await openDetail(detail.value.id);
  } catch (e: any) {
    toast(e.message || t('packages.stateSaveFailed'), 'error');
  } finally {
    savingState.value = false;
  }
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
  } catch (e: any) {
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

async function uninstall() {
  if (!detail.value) return;
  uninstalling.value = true;
  try {
    const resp = await fetch(`/api/v1/api/packages/${detail.value.id}`, { method: 'DELETE' });
    if (!resp.ok) throw new Error('uninstall failed');
    detailDialog.value = false;
    toast(t('packages.uninstalled'));
    await loadPackages();
  } catch (e) {
    console.error(e);
    toast(t('packages.uninstallFailed'), 'error');
  } finally {
    uninstalling.value = false;
  }
}

async function runExecute() {
  if (!detail.value || !execCommand.value) return;
  let args: any = {};
  if (execArgs.value.trim()) {
    try {
      args = JSON.parse(execArgs.value);
    } catch {
      toast(t('packages.invalidJson'), 'error');
      return;
    }
  }
  executing.value = true;
  execResult.value = null;
  try {
    const resp = await fetch(`/api/v1/api/packages/${detail.value.id}/execute`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ command: execCommand.value, args }),
    });
    execResult.value = await resp.json().catch(() => ({ error: 'no response body', success: false }));
  } catch (e: any) {
    execResult.value = { error: e.message, success: false };
  } finally {
    executing.value = false;
  }
}

loadPackages();
</script>

<style scoped>
.exec-ok {
  background: rgba(76, 175, 80, 0.08);
}
.exec-error {
  background: rgba(244, 67, 54, 0.08);
}
</style>
