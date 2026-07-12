<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import { useI18n } from 'vue-i18n';

import { apiGet } from '../lib/api';
import { formatUptime, relativeTime, truncate, formatNumber } from '../lib/format';

const { t } = useI18n();
const router = useRouter();

// ---------------------------------------------------------------------------
// Types (matching actual API responses)
// ---------------------------------------------------------------------------

interface StatusResponse {
  status?: string;
  uptime?: number;
}

interface Agent {
  id: string;
  name: string;
  description?: string;
  identity?: string;
  model_provider?: string;
  model_id?: string;
}

interface Session {
  id: number;
  conversation_id?: string;
  source?: string;
  status?: string;
  message_count?: number;
  last_active_unix_ms?: number;
  created_unix_ms?: number;
  session_type?: string;
}

interface Provider {
  provider_id: string;
  provider_type?: string;
  default_model?: string;
  base_url?: string;
  last_error?: string;
  auth_type?: string;
}

interface MemoryLayer {
  name: string;
  observation_count: number;
  horizon?: string;
  cron_expr?: string;
  enabled: boolean;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const loading = ref(true);

// Status ribbon
const status = ref<string>('unknown');
const uptime = ref<number | null>(null);
const agentCount = ref(0);
const sessionCount = ref(0);

// Sessions
const recentSessions = ref<Session[]>([]);
const sessionsError = ref('');

// Memory
const layers = ref<MemoryLayer[]>([]);
const memoryError = ref('');

// Providers
const providers = ref<Provider[]>([]);
const providersError = ref('');

// First-run banner
const showBanner = ref(false);

// ---------------------------------------------------------------------------
// Computed
// ---------------------------------------------------------------------------

const statusColor = computed(() => {
  const s = status.value.toLowerCase();
  if (s === 'ok' || s === 'running') return 'success';
  if (s === 'degraded' || s === 'warning') return 'warning';
  if (s === 'error' || s === 'stopped') return 'error';
  return 'grey';
});

const totalObservations = computed(() =>
  layers.value.reduce((sum, l) => sum + (l.observation_count || 0), 0)
);

const activeProviders = computed(() => providers.value.length);
const errorProviders = computed(() =>
  providers.value.filter(p => p.last_error && p.last_error.length > 0).length
);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function sessionLabel(s: Session): string {
  if (s.source === 'ws_chat') return 'Web';
  if (s.source?.startsWith('telegram')) return 'Telegram';
  if (s.source?.startsWith('discord')) return 'Discord';
  if (s.source?.startsWith('whatsapp')) return 'WhatsApp';
  if (s.source?.startsWith('irc')) return 'IRC';
  return s.source || 'Session';
}

function providerTypeLabel(type?: string): string {
  if (!type) return '';
  const map: Record<string, string> = {
    openai_compatible: 'OpenAI',
    anthropic: 'Anthropic',
    ollama: 'Ollama',
    google: 'Google',
    mistral: 'Mistral',
    zhipu: 'Z.ai',
    openai_codex: 'Codex',
  };
  return map[type] || type;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

function goToChat() { router.push('/chat'); }
function goToAgents() { router.push('/agents'); }
function goToProviders() { router.push('/providers'); }
function goToLogs() { router.push('/logs'); }
function goToWizard() { router.push('/wizard'); }
function goToMemory() { router.push('/memory'); }
function goToScheduler() { router.push('/scheduler'); }

function dismissBanner() {
  showBanner.value = false;
  localStorage.setItem('animus_wizard_dismissed', '1');
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

onMounted(async () => {
  // Parallel loads — each independent
  const results = await Promise.allSettled([
    apiGet<StatusResponse>('/api/v1/status'),
    apiGet<{ agents?: Agent[] }>('/api/v1/agents'),
    apiGet<{ sessions?: Session[]; total?: number }>('/api/v1/sessions?limit=8'),
    apiGet<{ providers?: Provider[] }>('/api/v1/providers'),
    apiGet<MemoryLayer[]>('/api/v1/memory/layers'),
  ]);

  loading.value = false;

  // Status
  if (results[0].status === 'fulfilled') {
    status.value = results[0].value.status ?? 'unknown';
    uptime.value = results[0].value.uptime ?? null;
  }

  // Agents
  if (results[1].status === 'fulfilled') {
    const agents = results[1].value.agents ?? [];
    agentCount.value = agents.length;
    if (agents.length === 0 && !localStorage.getItem('animus_wizard_dismissed')) {
      showBanner.value = true;
    }
  }

  // Sessions
  if (results[2].status === 'fulfilled') {
    const allSessions = results[2].value.sessions ?? [];
    sessionCount.value = allSessions.filter(s => s.status === 'active').length;
    // Sort by last_active desc, take 6
    recentSessions.value = allSessions
      .sort((a, b) => (b.last_active_unix_ms ?? 0) - (a.last_active_unix_ms ?? 0))
      .slice(0, 6);
  } else {
    sessionsError.value = t('dashboard.errors.sessions');
  }

  // Providers
  if (results[3].status === 'fulfilled') {
    providers.value = results[3].value.providers ?? [];
  } else {
    providersError.value = t('dashboard.errors.providers');
  }

  // Memory layers
  if (results[4].status === 'fulfilled') {
    layers.value = Array.isArray(results[4].value) ? results[4].value : [];
  } else {
    memoryError.value = t('dashboard.errors.memory');
  }
});
</script>

<template>
  <div>
    <!-- First-run banner -->
    <v-alert
      v-if="showBanner"
      type="info" variant="tonal" rounded="xl" class="mb-4"
      closable @click:close="dismissBanner"
    >
      <div class="d-flex align-center justify-space-between flex-wrap ga-3">
        <div><strong>{{ t('dashboard.banner.welcome') }}</strong> {{ t('dashboard.banner.welcomeSub') }}</div>
        <v-btn color="primary" size="small" variant="flat" @click="goToWizard">{{ t('dashboard.banner.beginSetup') }}</v-btn>
      </div>
    </v-alert>

    <!-- Loading state -->
    <div v-if="loading" class="d-flex justify-center align-center py-12">
      <v-progress-circular indeterminate size="40" />
    </div>

    <div v-else>
      <!-- ================================================================ -->
      <!-- Status ribbon                                                     -->
      <!-- ================================================================ -->
      <div class="status-ribbon mb-4 pa-3 rounded-lg d-flex align-center flex-wrap ga-4">
        <div class="d-flex align-center ga-2">
          <span class="status-dot" :class="`dot-${statusColor}`" />
          <span class="text-body-1 font-weight-medium">{{ status }}</span>
        </div>
        <div v-if="uptime != null" class="ribbon-item">
          <span class="text-caption text-medium-emphasis">{{ t('dashboard.ribbon.uptime') }}</span>
          <span class="text-body-2 font-weight-medium ml-1">{{ formatUptime(uptime) }}</span>
        </div>
        <div class="ribbon-item clickable" @click="goToAgents">
          <span class="text-caption text-medium-emphasis">{{ t('dashboard.ribbon.agents') }}</span>
          <span class="text-body-2 font-weight-medium ml-1">{{ agentCount }}</span>
        </div>
        <div class="ribbon-item clickable" @click="goToChat">
          <span class="text-caption text-medium-emphasis">{{ t('dashboard.ribbon.sessions') }}</span>
          <span class="text-body-2 font-weight-medium ml-1">{{ sessionCount }}</span>
        </div>
        <div class="ribbon-item clickable" @click="goToProviders">
          <span class="text-caption text-medium-emphasis">{{ t('dashboard.ribbon.providers') }}</span>
          <span class="text-body-2 font-weight-medium ml-1">{{ activeProviders }}</span>
          <span v-if="errorProviders > 0" class="text-caption text-error ml-1">({{ errorProviders }} {{ t('dashboard.ribbon.errors') }})</span>
        </div>
      </div>

      <!-- ================================================================ -->
      <!-- Row 2: Three cards                                               -->
      <!-- ================================================================ -->
      <div class="grid-3 mb-4">
        <!-- Recent Sessions -->
        <v-card rounded="xl" class="panel">
          <v-card-title class="text-subtitle-1 d-flex align-center justify-space-between">
            {{ t('dashboard.recentSessions.title') }}
            <v-btn size="x-small" variant="text" @click="goToChat">{{ t('dashboard.recentSessions.viewAll') }}</v-btn>
          </v-card-title>
          <v-card-text class="pt-0">
            <div v-if="sessionsError" class="text-error text-caption">{{ sessionsError }}</div>
            <div v-else-if="recentSessions.length === 0" class="text-body-2 text-medium-emphasis py-4 text-center">
              {{ t('dashboard.recentSessions.empty') }} <a @click="goToChat" class="text-primary">{{ t('dashboard.recentSessions.startChat') }}</a>
            </div>
            <v-list v-else density="compact" class="pa-0">
              <div v-for="s in recentSessions" :key="s.id" class="session-row" @click="goToChat">
                <div class="d-flex align-center justify-space-between">
                  <div class="d-flex align-center ga-2">
                    <v-chip size="x-small" variant="tonal" color="primary" density="compact">
                      {{ sessionLabel(s) }}
                    </v-chip>
                    <span class="text-body-2">{{ s.message_count ?? 0 }} {{ t('dashboard.recentSessions.messages') }}</span>
                  </div>
                  <span class="text-caption text-medium-emphasis">
                    {{ relativeTime(s.last_active_unix_ms) }}
                  </span>
                </div>
              </div>
            </v-list>
          </v-card-text>
        </v-card>

        <!-- Memory Health -->
        <v-card rounded="xl" class="panel">
          <v-card-title class="text-subtitle-1 d-flex align-center justify-space-between">
            {{ t('dashboard.memory.title') }}
            <v-btn size="x-small" variant="text" @click="goToMemory">{{ t('dashboard.memory.inspect') }}</v-btn>
          </v-card-title>
          <v-card-text class="pt-0">
            <div v-if="memoryError" class="text-error text-caption">{{ memoryError }}</div>
            <div v-else-if="layers.length === 0" class="text-body-2 text-medium-emphasis py-4 text-center">
              {{ t('dashboard.memory.empty') }}
            </div>
            <div v-else>
              <div class="d-flex align-center mb-3">
                <span class="text-h6 font-weight-bold">{{ formatNumber(totalObservations) }}</span>
                <span class="text-caption text-medium-emphasis ml-2">{{ t('dashboard.memory.totalObservations') }}</span>
              </div>
              <div class="d-flex flex-wrap ga-2">
                <div v-for="layer in layers.slice(0, 5)" :key="layer.name"
                  class="layer-chip"
                  :class="{ 'layer-disabled': !layer.enabled }"
                >
                  <span class="layer-name">{{ layer.name }}</span>
                  <span class="layer-count">{{ layer.observation_count }}</span>
                </div>
              </div>
            </div>
          </v-card-text>
        </v-card>

        <!-- Provider Status -->
        <v-card rounded="xl" class="panel">
          <v-card-title class="text-subtitle-1 d-flex align-center justify-space-between">
            {{ t('dashboard.providers.title') }}
            <v-btn size="x-small" variant="text" @click="goToProviders">{{ t('dashboard.providers.manage') }}</v-btn>
          </v-card-title>
          <v-card-text class="pt-0">
            <div v-if="providersError" class="text-error text-caption">{{ providersError }}</div>
            <div v-else-if="providers.length === 0" class="text-body-2 text-medium-emphasis py-4 text-center">
              {{ t('dashboard.providers.empty') }}
              <br>
              <a @click="goToWizard" class="text-primary">{{ t('dashboard.providers.runWizard') }}</a>
            </div>
            <v-list v-else density="compact" class="pa-0">
              <div v-for="p in providers" :key="p.provider_id" class="provider-row">
                <div class="d-flex align-center justify-space-between">
                  <div class="d-flex align-center ga-2">
                    <span
                      class="status-dot"
                      :class="p.last_error ? 'dot-error' : 'dot-success'"
                    />
                    <span class="text-body-2 font-weight-medium">{{ p.provider_id }}</span>
                  </div>
                  <div class="d-flex align-center ga-2">
                    <v-chip v-if="p.provider_type" size="x-small" variant="outlined" density="compact">
                      {{ providerTypeLabel(p.provider_type) }}
                    </v-chip>
                    <span class="text-caption text-medium-emphasis">{{ truncate(p.default_model || '', 20) }}</span>
                  </div>
                </div>
                <div v-if="p.last_error" class="text-caption text-error ml-6 mt-0">
                  {{ truncate(p.last_error, 60) }}
                </div>
              </div>
            </v-list>
          </v-card-text>
        </v-card>
      </div>

      <!-- ================================================================ -->
      <!-- Row 3: Quick Actions                                             -->
      <!-- ================================================================ -->
      <div class="grid-3">
        <v-card rounded="xl" class="panel">
          <v-card-title class="text-subtitle-1">{{ t('dashboard.quickActions.title') }}</v-card-title>
          <v-card-text class="pt-0">
            <div class="d-flex flex-wrap ga-2">
              <v-btn variant="tonal" color="primary" size="small" prepend-icon="mdi-chat" @click="goToChat">
                {{ t('dashboard.quickActions.newChat') }}
              </v-btn>
              <v-btn variant="tonal" size="small" prepend-icon="mdi-account-plus" @click="goToAgents">
                {{ t('dashboard.quickActions.addAgent') }}
              </v-btn>
              <v-btn variant="tonal" size="small" prepend-icon="mdi-cog" @click="goToProviders">
                {{ t('dashboard.quickActions.provider') }}
              </v-btn>
              <v-btn variant="tonal" size="small" prepend-icon="mdi-file-document" @click="goToLogs">
                {{ t('dashboard.quickActions.logs') }}
              </v-btn>
              <v-btn variant="tonal" size="small" prepend-icon="mdi-clock-outline" @click="goToScheduler">
                {{ t('dashboard.quickActions.scheduler') }}
              </v-btn>
              <v-btn variant="tonal" size="small" prepend-icon="mdi-brain" @click="goToMemory">
                {{ t('dashboard.quickActions.memory') }}
              </v-btn>
            </div>
          </v-card-text>
        </v-card>

        <v-card rounded="xl" class="panel">
          <v-card-title class="text-subtitle-1">{{ t('dashboard.daemonInfo.title') }}</v-card-title>
          <v-card-text class="pt-0">
            <div class="metric">
              <span class="metric-label">{{ t('dashboard.daemonInfo.status') }}</span>
              <span class="metric-value" :class="`text-${statusColor}`">{{ status }}</span>
            </div>
            <div v-if="uptime != null" class="metric">
              <span class="metric-label">{{ t('dashboard.daemonInfo.uptime') }}</span>
              <span class="metric-value font-monospace">{{ formatUptime(uptime) }}</span>
            </div>
            <div class="metric">
              <span class="metric-label">{{ t('dashboard.daemonInfo.agents') }}</span>
              <span class="metric-value font-monospace">{{ agentCount }}</span>
            </div>
            <div class="metric">
              <span class="metric-label">{{ t('dashboard.daemonInfo.activeSessions') }}</span>
              <span class="metric-value font-monospace">{{ sessionCount }}</span>
            </div>
            <div class="metric">
              <span class="metric-label">{{ t('dashboard.daemonInfo.providers') }}</span>
              <span class="metric-value font-monospace">{{ activeProviders }}</span>
            </div>
          </v-card-text>
        </v-card>

        <v-card rounded="xl" class="panel">
          <v-card-title class="text-subtitle-1">{{ t('dashboard.memoryLayers.title') }}</v-card-title>
          <v-card-text class="pt-0">
            <div v-if="layers.length === 0" class="text-body-2 text-medium-emphasis">
              {{ t('dashboard.memoryLayers.empty') }}
            </div>
            <div v-else>
              <div v-for="layer in layers" :key="layer.name" class="metric">
                <span class="metric-label">{{ layer.name }}</span>
                <span class="metric-value font-monospace">{{ formatNumber(layer.observation_count) }}</span>
              </div>
            </div>
          </v-card-text>
        </v-card>
      </div>
    </div>
  </div>
</template>

<style scoped>
/* Status ribbon */
.status-ribbon {
  background: rgba(var(--v-theme-surface-variant), 0.3);
  border: 1px solid rgba(var(--v-theme-on-surface), 0.08);
}

.ribbon-item {
  display: flex;
  align-items: baseline;
  gap: 2px;
}

.ribbon-item.clickable {
  cursor: pointer;
  transition: opacity 0.15s;
}
.ribbon-item.clickable:hover {
  opacity: 0.7;
}

/* Status dots */
.status-dot {
  display: inline-block;
  width: 10px;
  height: 10px;
  border-radius: 50%;
}
.dot-success { background: rgb(var(--v-theme-success)); box-shadow: 0 0 6px rgba(76, 175, 80, 0.4); }
.dot-warning { background: #FF9800; }
.dot-error { background: rgb(var(--v-theme-error)); }
.dot-grey { background: #9E9E9E; }

/* Grids */
.grid-3 {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  gap: 1rem;
}

/* Panels */
.panel {
  padding: 0.25rem;
}

/* Session rows */
.session-row {
  padding: 0.5rem 0;
  border-bottom: 1px solid rgba(var(--v-theme-on-surface), 0.06);
  cursor: pointer;
  transition: background 0.1s;
}
.session-row:hover {
  background: rgba(var(--v-theme-on-surface), 0.03);
  border-radius: 4px;
}
.session-row:last-child {
  border-bottom: none;
}

/* Provider rows */
.provider-row {
  padding: 0.35rem 0;
  border-bottom: 1px solid rgba(var(--v-theme-on-surface), 0.06);
}
.provider-row:last-child {
  border-bottom: none;
}

/* Layer chips */
.layer-chip {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 0.35rem 0.75rem;
  border-radius: 8px;
  background: rgba(var(--v-theme-primary), 0.08);
  min-width: 60px;
}
.layer-name {
  font-size: 0.7rem;
  text-transform: capitalize;
  color: rgba(var(--v-theme-on-surface), 0.6);
}
.layer-count {
  font-size: 1rem;
  font-weight: 600;
  font-family: monospace;
}
.layer-disabled {
  opacity: 0.4;
}

/* Metrics */
.metric {
  display: flex;
  justify-content: space-between;
  padding: 0.3rem 0;
  border-bottom: 1px solid rgba(var(--v-theme-on-surface), 0.04);
}
.metric:last-child { border-bottom: none; }
.metric-label { color: rgba(var(--v-theme-on-surface), 0.55); font-size: 0.875rem; }
.metric-value { font-size: 0.875rem; }
.font-monospace { font-family: monospace; }
</style>
