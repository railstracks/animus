<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { apiGet, apiRequest } from '../lib/api';

const { t } = useI18n();

// ── Types ──────────────────────────────────────────────────────────────────

interface NodeInfo {
  name: string;
  status: string;
  hostname: string;
  os: string;
  tools: string[];
  connected_at_unix_ms: number;
  last_heartbeat_unix_ms: number;
}

interface NodeToken {
  id: number;
  token_preview: string;
  created_at_unix_ms: number;
  label: string;
}

// ── State ──────────────────────────────────────────────────────────────────

const nodes = ref<NodeInfo[]>([]);
const tokens = ref<NodeToken[]>([]);
const loading = ref(true);
const error = ref('');
const successMsg = ref('');
const showTokenDialog = ref(false);
const showCopyDialog = ref(false);
const newTokenLabel = ref('');
const createdToken = ref('');
const copyConfirmed = ref(false);
const refreshing = ref(false);
let pollTimer: ReturnType<typeof setInterval> | null = null;

// ── Computed ───────────────────────────────────────────────────────────────

const connectedCount = computed(() => nodes.value.filter(n => n.status === 'online').length);

// ── Actions ────────────────────────────────────────────────────────────────

async function fetchData() {
  error.value = '';
  try {
    const [nodesRes, tokensRes] = await Promise.all([
      apiGet<{ nodes: NodeInfo[] }>('/api/v1/nodes'),
      apiGet<{ tokens: NodeToken[] }>('/api/v1/nodes/tokens'),
    ]);
    nodes.value = nodesRes.nodes ?? [];
    tokens.value = tokensRes.tokens ?? [];
  } catch (e: unknown) {
    error.value = e instanceof Error ? e.message : 'Failed to load node data';
  } finally {
    loading.value = false;
    refreshing.value = false;
  }
}

async function refreshNodes() {
  refreshing.value = true;
  try {
    const res = await apiGet<{ nodes: NodeInfo[] }>('/api/v1/nodes');
    nodes.value = res.nodes ?? [];
  } catch {
    // silent refresh
  } finally {
    refreshing.value = false;
  }
}

async function createToken() {
  error.value = '';
  successMsg.value = '';
  try {
    const res = await apiRequest<{ id: number; token: string; label: string }>(
      '/api/v1/nodes/tokens',
      'POST',
      { label: newTokenLabel.value || undefined },
    );
    createdToken.value = res.token;
    showTokenDialog.value = false;
    showCopyDialog.value = true;
    copyConfirmed.value = false;
    newTokenLabel.value = '';
    // Refresh token list
    const tokensRes = await apiGet<{ tokens: NodeToken[] }>('/api/v1/nodes/tokens');
    tokens.value = tokensRes.tokens ?? [];
  } catch (e: unknown) {
    error.value = e instanceof Error ? e.message : 'Failed to create token';
  }
}

async function revokeToken(id: number) {
  error.value = '';
  successMsg.value = '';
  try {
    await apiRequest(`/api/v1/nodes/tokens/${id}`, 'DELETE');
    tokens.value = tokens.value.filter(t => t.id !== id);
    successMsg.value = 'Token revoked';
    setTimeout(() => { successMsg.value = ''; }, 3000);
  } catch (e: unknown) {
    error.value = e instanceof Error ? e.message : 'Failed to revoke token';
  }
}

function closeCopyDialog() {
  if (!copyConfirmed.value) return;
  showCopyDialog.value = false;
  createdToken.value = '';
  copyConfirmed.value = false;
}

function copyToken() {
  navigator.clipboard.writeText(createdToken.value);
  copyConfirmed.value = true;
}

function formatDuration(ms: number): string {
  if (!ms || ms <= 0) return '—';
  const seconds = Math.floor(ms / 1000);
  if (seconds < 60) return `${seconds}s`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m ${seconds % 60}s`;
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return `${hours}h ${minutes % 60}m`;
  const days = Math.floor(hours / 24);
  return `${days}d ${hours % 24}h`;
}

function formatTime(unixMs: number): string {
  if (!unixMs) return '—';
  return new Date(unixMs).toLocaleTimeString();
}

function formatDate(unixMs: number): string {
  if (!unixMs) return '—';
  return new Date(unixMs).toLocaleDateString(undefined, {
    year: 'numeric', month: 'short', day: 'numeric',
    hour: '2-digit', minute: '2-digit',
  });
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

onMounted(() => {
  fetchData();
  pollTimer = setInterval(refreshNodes, 10000);
});

onBeforeUnmount(() => {
  if (pollTimer) clearInterval(pollTimer);
});
</script>

<template>
  <div>
    <div class="d-flex align-center mb-4">
      <h1 class="text-h5 mr-4">Nodes</h1>
      <v-chip size="small" color="primary" variant="tonal">
        {{ connectedCount }} connected
      </v-chip>
      <v-spacer />
      <v-btn
        variant="text"
        size="small"
        :loading="refreshing"
        prepend-icon="mdi-refresh"
        @click="refreshNodes"
      >
        Refresh
      </v-btn>
    </div>

    <v-alert v-if="error" type="error" variant="tonal" closable class="mb-4" @click:close="error = ''" />
    <v-alert v-if="successMsg" type="success" variant="tonal" closable class="mb-4" @click:close="successMsg = ''" />

    <!-- Connected Nodes -->
    <v-card variant="outlined" class="mb-6">
      <v-card-title class="text-subtitle-1 d-flex align-center">
        <v-icon class="mr-2">mdi-lan-connect</v-icon>
        Connected Nodes
      </v-card-title>
      <v-divider />

      <v-progress-linear v-if="loading" indeterminate color="primary" />

      <div v-if="!loading && nodes.length === 0" class="pa-8 text-center text-medium-emphasis">
        <v-icon size="48" class="mb-2" color="grey-lighten-1">mdi-lan-disconnect</v-icon>
        <div class="text-body-1">No nodes connected</div>
        <div class="text-body-2 mt-1">Create a token below and connect a node daemon.</div>
      </div>

      <v-list v-if="nodes.length > 0" lines="three">
        <template v-for="(node, i) in nodes" :key="node.name">
          <v-list-item v-if="i > 0" :key="`div-${i}`" />
          <v-list-item>
            <template #prepend>
              <v-avatar :color="node.status === 'online' ? 'success' : 'grey'" size="36">
                <v-icon color="white">mdi-{{
                  node.status === 'online' ? 'lan-connect' : 'lan-disconnect'
                }}</v-icon>
              </v-avatar>
            </template>

            <v-list-item-title class="font-weight-medium">
              {{ node.name }}
              <v-chip
                size="x-small"
                :color="node.status === 'online' ? 'success' : 'default'"
                variant="flat"
                class="ml-2"
              >
                {{ node.status }}
              </v-chip>
            </v-list-item-title>

            <v-list-item-subtitle class="text-body-2">
              {{ node.hostname || 'unknown' }} · {{ node.os || 'unknown' }}
            </v-list-item-subtitle>

            <div class="mt-2 d-flex flex-wrap gap-2 align-center">
              <v-chip
                v-for="tool in node.tools"
                :key="tool"
                size="x-small"
                variant="outlined"
                color="primary"
              >
                {{ tool }}
              </v-chip>
            </div>

            <div class="mt-1 text-caption text-medium-emphasis">
              Connected: {{ formatDate(node.connected_at_unix_ms) }}
              · Last heartbeat: {{ formatTime(node.last_heartbeat_unix_ms) }}
              · Uptime: {{ formatDuration(Date.now() - node.connected_at_unix_ms) }}
            </div>
          </v-list-item>
        </template>
      </v-list>
    </v-card>

    <!-- Auth Tokens -->
    <v-card variant="outlined">
      <v-card-title class="text-subtitle-1 d-flex align-center">
        <v-icon class="mr-2">mdi-key-variant</v-icon>
        Auth Tokens
        <v-spacer />
        <v-btn
          size="small"
          color="primary"
          variant="tonal"
          prepend-icon="mdi-plus"
          @click="showTokenDialog = true"
        >
          Generate Token
        </v-btn>
      </v-card-title>
      <v-divider />

      <div v-if="!loading && tokens.length === 0" class="pa-8 text-center text-medium-emphasis">
        <v-icon size="48" class="mb-2" color="grey-lighten-1">mdi-key-outline</v-icon>
        <div class="text-body-1">No tokens issued</div>
      </div>

      <v-list v-if="tokens.length > 0" lines="two">
        <v-list-item
          v-for="token in tokens"
          :key="token.id"
        >
          <template #prepend>
            <v-icon color="amber-darken-2">mdi-key</v-icon>
          </template>

          <v-list-item-title class="font-mono">
            {{ token.token_preview }}
          </v-list-item-title>
          <v-list-item-subtitle>
            {{ token.label || 'unlabeled' }} · Created {{ formatDate(token.created_at_unix_ms) }}
          </v-list-item-subtitle>

          <template #append>
            <v-btn
              icon="mdi-delete-outline"
              variant="text"
              size="small"
              color="error"
              @click="revokeToken(token.id)"
            />
          </template>
        </v-list-item>
      </v-list>
    </v-card>

    <!-- Quick Start Guide -->
    <v-card variant="outlined" class="mt-6 pa-4">
      <div class="text-h6 mb-2">
        <v-icon class="mr-1">mdi-help-circle-outline</v-icon>
        Connecting a Node
      </div>
      <div class="text-body-2 text-medium-emphasis">
        Generate a token, then run the daemon on any machine:
      </div>
      <v-code class="mt-2 pa-3 text-body-2">
        animusd --node \<br>
        &nbsp;&nbsp;--server-url ws://SERVER:8080/ws/node \<br>
        &nbsp;&nbsp;--token YOUR_TOKEN \<br>
        &nbsp;&nbsp;--node-name my-worker \<br>
        &nbsp;&nbsp;--node-tools exec,file
      </v-code>
    </v-card>

    <!-- Generate Token Dialog -->
    <v-dialog v-model="showTokenDialog" max-width="500">
      <v-card>
        <v-card-title>Generate Node Token</v-card-title>
        <v-card-text>
          <v-text-field
            v-model="newTokenLabel"
            label="Label (optional)"
            hint="A descriptive name for this token"
            variant="outlined"
            density="compact"
            @keyup.enter="createToken"
          />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="showTokenDialog = false">Cancel</v-btn>
          <v-btn color="primary" variant="tonal" @click="createToken">Generate</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Copy Token Dialog (shown once after creation) -->
    <v-dialog v-model="showCopyDialog" max-width="550" persistent>
      <v-card>
        <v-card-title class="text-warning">
          <v-icon class="mr-1">mdi-alert</v-icon>
          Copy Your Token
        </v-card-title>
        <v-card-text>
          <v-alert type="warning" variant="tonal" class="mb-3">
            This token is shown only once. Store it securely.
          </v-alert>
          <v-textarea
            :model-value="createdToken"
            readonly
            rows="3"
            variant="outlined"
            density="compact"
            class="font-mono"
          />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="copyToken" prepend-icon="mdi-content-copy">
            Copy &amp; Confirm
          </v-btn>
          <v-btn color="primary" :disabled="!copyConfirmed" @click="closeCopyDialog">Done</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </div>
</template>

<style scoped>
.gap-2 {
  gap: 8px;
}
</style>
