<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { apiGet, apiRequest } from '../lib/api';

const { t } = useI18n();

interface Capabilities {
  model_id: string;
  context_length: number;
  supports_tools: boolean;
  supports_tool_choice: boolean;
  supports_reasoning: boolean;
  supports_streaming: boolean;
  supports_json_mode: boolean;
  supports_vision: boolean;
  raw_features: string[];
}

interface ProviderInfo {
  provider_id: string;
  provider_type: string;
  base_url: string;
  default_model: string;
  default_context_window?: number;
  model_context_windows?: Record<string, number>;
  auth_type: string;
  api_key: string;
  status: string;
  last_error: string;
  last_tested_unix_ms: number;
  concurrency: number;
  auth_file?: string;
  extra: Record<string, string>;
  capabilities?: Capabilities;
}

interface ProvidersResponse {
  default_provider: string;
  providers: ProviderInfo[];
}

const knownProviderTypes: { id: string; label: string; baseUrl: string; defaultModel: string; authType: string }[] = [
  { id: 'openai', label: 'OpenAI', baseUrl: 'https://api.openai.com/v1', defaultModel: 'gpt-5.2', authType: 'api_key' },
  { id: 'openai-codex', label: 'OpenAI Codex', baseUrl: 'https://chatgpt.com/backend-api/codex', defaultModel: 'gpt-5.4', authType: 'oauth' },
  { id: 'zai', label: 'Z.AI (GLM)', baseUrl: 'https://api.z.ai/api/paas/v4', defaultModel: 'glm-5.1', authType: 'api_key' },
  { id: 'ollama', label: 'Ollama', baseUrl: 'https://ollama.com/v1', defaultModel: 'llama3', authType: 'api_key' },
  { id: 'cohere', label: 'Cohere', baseUrl: 'https://api.cohere.com/v2', defaultModel: 'command-a-03-2025', authType: 'api_key' },
  { id: 'mistral', label: 'Mistral', baseUrl: 'https://api.mistral.ai/v1', defaultModel: 'mistral-large-latest', authType: 'api_key' },
  { id: 'alibaba', label: 'Alibaba Cloud (Qwen)', baseUrl: 'https://dashscope-intl.aliyuncs.com/compatible-mode/v1', defaultModel: 'qwen-plus', authType: 'api_key' },
  { id: 'openai-compatible', label: 'OpenAI-Compatible (Generic)', baseUrl: '', defaultModel: '', authType: 'api_key' },
  { id: 'deepseek', label: 'DeepSeek', baseUrl: 'https://api.deepseek.com/v1', defaultModel: 'deepseek-v4-flash', authType: 'api_key' },
  { id: 'openrouter', label: 'OpenRouter', baseUrl: 'https://openrouter.ai/api/v1', defaultModel: 'deepseek/deepseek-v4-flash', authType: 'api_key' },
];

const providers = ref<ProviderInfo[]>([]);
const defaultProvider = ref('');
const loading = ref(true);
const error = ref('');
const successMsg = ref('');
const testingId = ref('');
const testResult = ref<{ id: string; success: boolean; error?: string } | null>(null);
const showForm = ref(false);
const editingProvider = ref<ProviderInfo | null>(null);

const formData = ref({
  provider_id: '',
  provider_type: '',
  base_url: '',
  default_model: '',
  default_context_window: 128000,
  model_context_windows_json: '{}',
  auth_type: 'api_key',
  api_key: '',
  auth_file: '',
  concurrency: 1,
});

const availableModels = ref<string[]>([]);
const modelsLoading = ref(false);
const modelsError = ref('');
const modelSearch = ref('');
const hasPersistedKey = ref(false); // true after saving with an API key
const oauthBusyProviderId = ref('');
const oauthError = ref('');
const oauthConnected = ref(false);
const oauthExpired = ref(true);
const oauthBrowserBusyProviderId = ref('');
const oauthBrowserExchangeProviderId = ref('');
const oauthBrowserRedirectUri = ref('http://localhost:1455/auth/callback');
const oauthBrowserCallbackInput = ref('');
const oauthBrowserFlow = ref<{
  providerId: string;
  authorizationUrl: string;
  redirectUri: string;
  state: string;
  expiresAt?: string;
} | null>(null);
const oauthFlow = ref<{
  providerId: string;
  status: 'idle' | 'pending' | 'authorized' | 'error';
  deviceAuthId: string;
  userCode: string;
  verificationUrl: string;
  intervalSeconds: number;
  expiresAt?: string;
  message?: string;
} | null>(null);
let oauthPollTimer: ReturnType<typeof setInterval> | null = null;

// Provider types that support model listing via capabilities endpoint
const modelListProviders = new Set(['cohere', 'ollama', 'openai-codex']);
// Provider types that require auth to fetch models
const authRequiredForModels = new Set(['cohere', 'openai-codex']);

const isNew = computed(() => !editingProvider.value);
const usedProviderIds = computed(() => new Set(providers.value.map(p => p.provider_id)));
const canFetchModels = computed(() => {
  const pt = formData.value.provider_type;
  if (!modelListProviders.has(pt)) return false;
  if (authRequiredForModels.has(pt)) {
    if (formData.value.auth_type === 'api_key') {
      // API-key providers need a saved key before model listing is possible.
      return hasPersistedKey.value;
    }
    if (formData.value.auth_type === 'oauth') {
      // OAuth providers need to exist first so backend can load auth context.
      return !isNew.value;
    }
    return false;
  }
  return true;
});
const canSaveAndContinue = computed(() => {
  if (formData.value.auth_type !== 'api_key') return true;
  return Boolean(formData.value.api_key) || hasPersistedKey.value;
});

function stopOauthPolling() {
  if (oauthPollTimer) {
    clearInterval(oauthPollTimer);
    oauthPollTimer = null;
  }
}

async function refreshOauthStatus(providerId: string) {
  try {
    const status = await apiGet<{ connected: boolean; expired?: boolean }>(
      `/api/v1/providers/${providerId}/oauth/status`
    );
    oauthConnected.value = Boolean(status.connected);
    oauthExpired.value = Boolean(status.expired ?? true);
  } catch {
    oauthConnected.value = false;
    oauthExpired.value = true;
  }
}

async function pollOauthDeviceFlow(providerId: string) {
  if (!oauthFlow.value || oauthFlow.value.providerId !== providerId) return;
  try {
    const out = await apiRequest<{ status: 'pending' | 'authorized'; message?: string }>(
      'POST',
      `/api/v1/providers/${providerId}/oauth/device/poll`,
      {
        device_auth_id: oauthFlow.value.deviceAuthId,
        user_code: oauthFlow.value.userCode,
      }
    );

    if (out.status === 'authorized') {
      oauthFlow.value = { ...oauthFlow.value, status: 'authorized', message: 'OAuth connected.' };
      oauthConnected.value = true;
      oauthExpired.value = false;
      stopOauthPolling();
      successMsg.value = `OAuth connected for ${providerId}`;
      await loadProviders();
      return;
    }

    oauthFlow.value = {
      ...oauthFlow.value,
      status: 'pending',
      message: out.message || 'Waiting for authorization...',
    };
  } catch (e: any) {
    oauthError.value = e.message || 'OAuth polling failed';
    if (oauthFlow.value) {
      oauthFlow.value = { ...oauthFlow.value, status: 'error', message: oauthError.value };
    }
    stopOauthPolling();
  }
}

async function startOauthBrowserFlow(providerId: string) {
  oauthBrowserBusyProviderId.value = providerId;
  oauthError.value = '';
  try {
    const out = await apiRequest<{
      authorization_url: string;
      redirect_uri: string;
      state: string;
      expires?: string;
    }>(
      'POST',
      `/api/v1/providers/${providerId}/oauth/browser/start`,
      {
        redirect_uri: oauthBrowserRedirectUri.value?.trim() || 'http://localhost:1455/auth/callback',
      }
    );

    oauthBrowserFlow.value = {
      providerId,
      authorizationUrl: out.authorization_url,
      redirectUri: out.redirect_uri,
      state: out.state,
      expiresAt: out.expires,
    };
    if (typeof window !== 'undefined') {
      window.open(out.authorization_url, '_blank', 'noopener,noreferrer');
    }
  } catch (e: any) {
    oauthError.value = e.message || 'Failed to start browser OAuth flow';
  } finally {
    oauthBrowserBusyProviderId.value = '';
  }
}

async function completeOauthBrowserFlow(providerId: string) {
  const callback = oauthBrowserCallbackInput.value?.trim();
  if (!callback) {
    oauthError.value = 'Paste the full callback URL first.';
    return;
  }
  oauthBrowserExchangeProviderId.value = providerId;
  oauthError.value = '';
  try {
    await apiRequest(
      'POST',
      `/api/v1/providers/${providerId}/oauth/browser/exchange`,
      { callback_url: callback }
    );
    oauthConnected.value = true;
    oauthExpired.value = false;
    oauthBrowserCallbackInput.value = '';
    oauthBrowserFlow.value = null;
    successMsg.value = `OAuth connected for ${providerId}`;
    await loadProviders();
  } catch (e: any) {
    oauthError.value = e.message || 'Failed to complete browser OAuth flow';
  } finally {
    oauthBrowserExchangeProviderId.value = '';
  }
}

async function startOauthDeviceFlow(providerId: string) {
  oauthBusyProviderId.value = providerId;
  oauthError.value = '';
  stopOauthPolling();
  try {
    const out = await apiRequest<{
      status: 'pending';
      device_auth_id: string;
      user_code: string;
      verification_url: string;
      interval_seconds?: number;
      expires_at?: string;
    }>('POST', `/api/v1/providers/${providerId}/oauth/device/start`);

    oauthFlow.value = {
      providerId,
      status: 'pending',
      deviceAuthId: out.device_auth_id,
      userCode: out.user_code,
      verificationUrl: out.verification_url,
      intervalSeconds: Math.max(1, Number(out.interval_seconds || 5)),
      expiresAt: out.expires_at,
      message: 'Open the verification URL and enter the code.',
    };

    if (typeof window !== 'undefined') {
      window.open(out.verification_url, '_blank', 'noopener,noreferrer');
    }

    oauthPollTimer = setInterval(() => {
      void pollOauthDeviceFlow(providerId);
    }, oauthFlow.value.intervalSeconds * 1000);
    void pollOauthDeviceFlow(providerId);
  } catch (e: any) {
    oauthError.value = e.message || 'Failed to start OAuth device flow';
    oauthFlow.value = {
      providerId,
      status: 'error',
      deviceAuthId: '',
      userCode: '',
      verificationUrl: '',
      intervalSeconds: 5,
      message: oauthError.value,
    };
  } finally {
    oauthBusyProviderId.value = '';
  }
}

function onProviderTypeChange(id: string) {
  const pt = knownProviderTypes.find(p => p.id === id);
  if (pt) {
    formData.value.provider_type = pt.id;
    formData.value.base_url = pt.baseUrl;
    formData.value.default_model = pt.defaultModel;
    formData.value.default_context_window = 128000;
    formData.value.auth_type = pt.authType;
    if (pt.authType === 'oauth') {
      formData.value.auth_file = 'config/auth.json';
    }
  }
  fetchModelsForType(id);
}

async function fetchModelsForType(providerType: string) {
  availableModels.value = [];
  modelsError.value = '';

  if (!modelListProviders.has(providerType)) return;
  if (!canFetchModels.value) return;

  modelsLoading.value = true;
  try {
    // No direct browser fetch — all model lists come via backend
    // (CORS blocks direct access to ollama.com/v1, api.cohere.com, etc.)
  } catch (e: any) {
    modelsError.value = e.message || 'Failed to fetch models';
  } finally {
    modelsLoading.value = false;
  }
}

async function fetchModelsForExisting(provider: ProviderInfo) {
  availableModels.value = [];
  modelsError.value = '';

  if (!modelListProviders.has(provider.provider_type)) {
    return;
  }

  // All providers: fetch via backend /models (avoids CORS + key exposure)
  modelsLoading.value = true;
  try {
    const data = await apiGet<{models: string[], fetched: boolean}>(
      `/api/v1/providers/${provider.provider_id}/models`
    );
    if (Array.isArray(data.models)) {
      availableModels.value = data.models.sort();
    }
  } catch (e: any) {
    modelsError.value = e.message || 'Failed to fetch models';
  } finally {
    modelsLoading.value = false;
  }
}

function generateDefaultName(typeId: string): string {
  const existing = providers.value.filter(p => p.provider_type === typeId);
  if (existing.length === 0) return typeId;
  return `${typeId}-${existing.length + 1}`;
}

async function loadProviders() {
  loading.value = true;
  error.value = '';
  try {
    const data = await apiGet<ProvidersResponse>('/api/v1/providers');
    providers.value = data.providers;
    defaultProvider.value = data.default_provider;
  } catch (e: any) {
    error.value = e.message || t('providers.errors.loadFailed');
  } finally {
    loading.value = false;
  }
}

async function testProvider(id: string) {
  testingId.value = id;
  testResult.value = null;
  try {
    const result = await apiRequest<{ success: boolean; error?: string; status: string }>(
      'POST', `/api/v1/providers/${id}/test`
    );
    testResult.value = { id, success: result.success, error: result.error };
    await loadProviders();
    if (result.success) {
      successMsg.value = t('providers.testSuccess', { id });
    }
  } catch (e: any) {
    testResult.value = { id, success: false, error: e.message };
  } finally {
    testingId.value = '';
    setTimeout(() => { testResult.value = null; }, 5000);
  }
}

async function deleteProvider(id: string) {
  if (!confirm(t('providers.actions.confirmDelete', { id }))) return;
  try {
    await apiRequest('DELETE', `/api/v1/providers/${id}`);
    await loadProviders();
    successMsg.value = t('providers.deleteSuccess', { id });
  } catch (e: any) {
    error.value = e.message;
  }
}

async function setDefault(id: string) {
  try {
    await apiRequest('PATCH', '/api/v1/providers/default', { provider_id: id });
    defaultProvider.value = id;
    successMsg.value = t('providers.defaultSuccess', { id });
  } catch (e: any) {
    error.value = e.message;
  }
}

function openCreate() {
  editingProvider.value = null;
  hasPersistedKey.value = false;
  availableModels.value = [];
  oauthFlow.value = null;
  oauthBrowserFlow.value = null;
  oauthBrowserCallbackInput.value = '';
  oauthBrowserRedirectUri.value = 'http://localhost:1455/auth/callback';
  oauthError.value = '';
  oauthConnected.value = false;
  oauthExpired.value = true;
  stopOauthPolling();
  const firstType = knownProviderTypes[0];
  formData.value = {
    provider_id: generateDefaultName(firstType.id),
    provider_type: firstType.id,
    base_url: firstType.baseUrl,
    default_model: firstType.defaultModel,
    default_context_window: 128000,
    model_context_windows_json: '{}',
    auth_type: firstType.authType,
    api_key: '',
    auth_file: firstType.authType === 'oauth' ? 'config/auth.json' : '',
    concurrency: 1,
  };
  showForm.value = true;
}

function openEdit(p: ProviderInfo) {
  editingProvider.value = p;
  hasPersistedKey.value = p.auth_type === 'api_key';
  availableModels.value = [];
  oauthFlow.value = null;
  oauthBrowserFlow.value = null;
  oauthBrowserCallbackInput.value = '';
  oauthBrowserRedirectUri.value = 'http://localhost:1455/auth/callback';
  oauthError.value = '';
  stopOauthPolling();
  formData.value = {
    provider_id: p.provider_id,
    provider_type: p.provider_type,
    base_url: p.base_url,
    default_model: p.default_model,
    default_context_window: p.default_context_window || 128000,
    model_context_windows_json: JSON.stringify(p.model_context_windows || {}, null, 2),
    auth_type: p.auth_type,
    concurrency: p.concurrency || 1,
    api_key: '',
    auth_file: p.auth_file || '',
  };
  fetchModelsForExisting(p);
  showForm.value = true;
  if (p.auth_type === 'oauth') {
    void refreshOauthStatus(p.provider_id);
  } else {
    oauthConnected.value = false;
    oauthExpired.value = true;
  }
}

function closeForm() {
  showForm.value = false;
  editingProvider.value = null;
  oauthBrowserFlow.value = null;
  oauthBrowserCallbackInput.value = '';
  stopOauthPolling();
}

async function submitForm() {
  try {
    let modelContextWindows: Record<string, number> = {};
    try {
      const parsed = JSON.parse(formData.value.model_context_windows_json || '{}');
      if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) {
        for (const [modelId, value] of Object.entries(parsed)) {
          const num = Number(value);
          if (!Number.isFinite(num) || num <= 0) continue;
          modelContextWindows[modelId] = Math.floor(num);
        }
      }
    } catch (_e: any) {
      error.value = t('providers.form.modelContextWindowsInvalid');
      return;
    }

    const payload: Record<string, unknown> = {
      ...formData.value,
      model_context_windows: modelContextWindows,
    };
    delete payload.model_context_windows_json;
    // On edit: don't send empty api_key — preserve the stored key
    if (!isNew.value && (!payload.api_key || (payload.api_key as string).trim() === '')) {
      delete payload.api_key;
    }
    if (isNew.value) {
      await apiRequest('POST', '/api/v1/providers', payload);
    } else {
      await apiRequest('PUT', `/api/v1/providers/${formData.value.provider_id}`, payload);
    }
    closeForm();
    await loadProviders();
    successMsg.value = isNew.value
      ? t('providers.createSuccess', { id: formData.value.provider_id })
      : t('providers.updateSuccess', { id: formData.value.provider_id });
  } catch (e: any) {
    error.value = e.message;
  }
}

async function saveAndContinue() {
  try {
    let modelContextWindows: Record<string, number> = {};
    try {
      const parsed = JSON.parse(formData.value.model_context_windows_json || '{}');
      if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) {
        for (const [modelId, value] of Object.entries(parsed)) {
          const num = Number(value);
          if (!Number.isFinite(num) || num <= 0) continue;
          modelContextWindows[modelId] = Math.floor(num);
        }
      }
    } catch (_e: any) {
      error.value = t('providers.form.modelContextWindowsInvalid');
      return;
    }

    const payload: Record<string, unknown> = {
      ...formData.value,
      model_context_windows: modelContextWindows,
    };
    delete payload.model_context_windows_json;
    if (!isNew.value && (!payload.api_key || (payload.api_key as string).trim() === '')) {
      delete payload.api_key;
    }
    if (isNew.value) {
      await apiRequest('POST', '/api/v1/providers', payload);
      // Now it's an existing provider — switch to edit mode
      editingProvider.value = { provider_id: formData.value.provider_id } as ProviderInfo;
    } else {
      await apiRequest('PUT', `/api/v1/providers/${formData.value.provider_id}`, payload);
    }
    hasPersistedKey.value = true;
    await loadProviders();
    successMsg.value = t('providers.form.savedContinue');
    fetchModelsForExisting({ provider_id: formData.value.provider_id, provider_type: formData.value.provider_type } as ProviderInfo);
    if (formData.value.auth_type === 'oauth') {
      await refreshOauthStatus(formData.value.provider_id);
    }
  } catch (e: any) {
    error.value = e.message;
  }
}

function statusIcon(status: string): string {
  switch (status) {
    case 'available': return '✅';
    case 'error': return '❌';
    default: return '⚠️';
  }
}

function statusColor(status: string): string {
  switch (status) {
    case 'available': return 'success';
    case 'error': return 'error';
    default: return 'warning';
  }
}

function providerTypeLabel(typeId: string): string {
  const pt = knownProviderTypes.find(p => p.id === typeId);
  return pt ? pt.label : typeId;
}

onMounted(loadProviders);
onBeforeUnmount(() => {
  stopOauthPolling();
});
</script>

<template>
  <v-card rounded="xl" class="panel">
    <v-card-title class="d-flex align-center justify-space-between">
      <span>{{ t('providers.title') }}</span>
      <div>
        <v-btn variant="text" size="small" @click="loadProviders" :loading="loading">
          {{ t('providers.actions.refresh') }}
        </v-btn>
        <v-btn color="primary" size="small" @click="openCreate">
          {{ t('providers.actions.add') }}
        </v-btn>
      </div>
    </v-card-title>

    <v-card-text>
      <v-alert v-if="error" type="error" closable @click:close="error = ''" class="mb-4">
        {{ error }}
      </v-alert>
      <v-alert v-if="successMsg" type="success" closable @click:close="successMsg = ''" class="mb-4">
        {{ successMsg }}
      </v-alert>
      <v-alert v-if="testResult && !testResult.success" type="warning" class="mb-4">
        {{ t('providers.testFailed', { id: testResult.id }) }}: {{ testResult.error }}
      </v-alert>

      <v-progress-linear v-if="loading" indeterminate class="mb-4" />

      <v-table v-if="!loading && providers.length" density="comfortable">
        <thead>
          <tr>
            <th>{{ t('providers.columns.status') }}</th>
            <th>{{ t('providers.columns.provider') }}</th>
            <th>{{ t('providers.columns.type') }}</th>
            <th>{{ t('providers.columns.baseUrl') }}</th>
            <th>{{ t('providers.columns.defaultModel') }}</th>
            <th>{{ t('providers.columns.default') }}</th>
            <th>{{ t('providers.columns.actions') }}</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="p in providers" :key="p.provider_id">
            <td>
              <v-chip :color="statusColor(p.status)" size="small" variant="tonal">
                {{ statusIcon(p.status) }}
                <v-tooltip v-if="p.last_error" activator="parent" location="bottom">
                  {{ p.last_error }}
                </v-tooltip>
              </v-chip>
            </td>
            <td><strong>{{ p.provider_id }}</strong></td>
            <td>{{ providerTypeLabel(p.provider_type || p.provider_id) }}</td>
            <td class="text-truncate" style="max-width: 200px;">{{ p.base_url }}</td>
            <td>{{ p.default_model }}</td>
            <td>
              <v-icon v-if="p.provider_id === defaultProvider" color="primary" size="small">
                mdi-star
              </v-icon>
              <v-btn v-else variant="text" size="x-small" @click="setDefault(p.provider_id)">
                {{ t('providers.actions.setDefault') }}
              </v-btn>
            </td>
            <td>
              <v-btn variant="text" size="x-small" @click="testProvider(p.provider_id)"
                     :loading="testingId === p.provider_id"
                     :color="testResult?.id === p.provider_id && testResult?.success ? 'success' : undefined">
                {{ testResult?.id === p.provider_id && testResult?.success ? '✓' : t('providers.actions.test') }}
              </v-btn>
              <v-btn variant="text" size="x-small" @click="openEdit(p)">
                {{ t('providers.actions.edit') }}
              </v-btn>
              <v-btn variant="text" size="x-small" color="error" @click="deleteProvider(p.provider_id)">
                {{ t('providers.actions.delete') }}
              </v-btn>
            </td>
          </tr>
        </tbody>
      </v-table>

      <v-empty-state v-if="!loading && !providers.length"
        :title="t('providers.empty.title')"
        :text="t('providers.empty.description')"
      />
    </v-card-text>

    <!-- Provider Create/Edit Dialog -->
    <v-dialog v-model="showForm" max-width="560">
      <v-card>
        <v-card-title>
          {{ isNew ? t('providers.form.createTitle') : t('providers.form.editTitle') }}
        </v-card-title>
        <v-card-text>
          <v-select v-if="isNew"
            v-model="formData.provider_type"
            :label="t('providers.form.providerType')"
            :items="knownProviderTypes"
            item-title="label"
            item-value="id"
            @update:model-value="onProviderTypeChange"
          />

          <v-text-field v-model="formData.provider_id"
            :label="t('providers.form.instanceName')"
            :disabled="!isNew"
            :hint="t('providers.form.instanceNameHint')"
            persistent-hint
          />

          <v-text-field v-model="formData.base_url"
            :label="t('providers.form.baseUrl')" />

          <v-combobox
            v-model="formData.default_model"
            :label="t('providers.form.defaultModel')"
            :items="availableModels"
            :loading="modelsLoading"
            :error-messages="modelsError"
            :search="modelSearch"
            :disabled="modelListProviders.has(formData.provider_type) && !canFetchModels"
            @update:search="modelSearch = $event"
            clearable
            hide-no-data
          />
          <p v-if="modelListProviders.has(formData.provider_type) && !canFetchModels" class="text-caption text-medium-emphasis mt-n2 mb-2">
            {{ t('providers.form.modelsLockedHint') }}
          </p>
          <p v-else-if="!modelListProviders.has(formData.provider_type)" class="text-caption text-medium-emphasis mt-n2 mb-2">
            {{ t('providers.form.modelManualHint') }}
          </p>

          <v-text-field
            v-model.number="formData.default_context_window"
            :label="t('providers.form.defaultContextWindow')"
            type="number"
            min="1"
          />

          <v-textarea
            v-model="formData.model_context_windows_json"
            :label="t('providers.form.modelContextWindows')"
            :hint="t('providers.form.modelContextWindowsHint')"
            persistent-hint
            rows="3"
            auto-grow
          />

          <v-select v-model="formData.auth_type"
            :label="t('providers.form.authType')"
            :items="['api_key', 'oauth', 'none']"
            :disabled="!isNew" />

          <v-text-field v-if="formData.auth_type === 'api_key'"
            v-model="formData.api_key"
            :label="t('providers.form.apiKey')"
            :placeholder="isNew ? '' : t('providers.form.apiKeyPlaceholder')"
            type="password" />

          <v-text-field v-if="formData.auth_type === 'oauth'"
            v-model="formData.auth_file"
            :label="t('providers.form.authFile')" />
          <div v-if="formData.auth_type === 'oauth'" class="text-caption mt-2 mb-2">
            <div v-if="isNew" class="text-medium-emphasis">
              Save this provider first, then connect OAuth.
            </div>
            <div v-else class="d-flex flex-column ga-2">
              <div class="d-flex align-center ga-2">
                <v-btn
                  size="small"
                  color="primary"
                  variant="tonal"
                  :loading="oauthBusyProviderId === formData.provider_id"
                  @click="startOauthDeviceFlow(formData.provider_id)"
                >
                  Connect OAuth
                </v-btn>
                <span>
                  Status:
                  <strong>{{ oauthConnected ? (oauthExpired ? 'Expired' : 'Connected') : 'Not connected' }}</strong>
                </span>
              </div>
              <div class="oauth-box">
                <div class="text-subtitle-2 mb-2">{{ t('providers.form.oauthBrowserTitle') }}</div>
                <v-text-field
                  v-model="oauthBrowserRedirectUri"
                  :label="t('providers.form.oauthRedirectUri')"
                  density="compact"
                  hide-details="auto"
                  class="mb-2"
                />
                <div class="d-flex align-center ga-2 mb-2">
                  <v-btn
                    size="small"
                    color="primary"
                    variant="tonal"
                    :loading="oauthBrowserBusyProviderId === formData.provider_id"
                    @click="startOauthBrowserFlow(formData.provider_id)"
                  >
                    {{ t('providers.form.oauthStartBrowser') }}
                  </v-btn>
                </div>
                <div v-if="oauthBrowserFlow && oauthBrowserFlow.providerId === formData.provider_id" class="mb-2">
                  <div><strong>{{ t('providers.form.oauthAuthorizationUrl') }}:</strong> {{ oauthBrowserFlow.authorizationUrl }}</div>
                  <div><strong>{{ t('providers.form.oauthState') }}:</strong> {{ oauthBrowserFlow.state }}</div>
                </div>
                <v-textarea
                  v-model="oauthBrowserCallbackInput"
                  :label="t('providers.form.oauthCallbackUrl')"
                  :hint="t('providers.form.oauthCallbackHint')"
                  persistent-hint
                  rows="2"
                  auto-grow
                />
                <div class="d-flex align-center ga-2 mt-2">
                  <v-btn
                    size="small"
                    color="primary"
                    variant="tonal"
                    :loading="oauthBrowserExchangeProviderId === formData.provider_id"
                    @click="completeOauthBrowserFlow(formData.provider_id)"
                  >
                    {{ t('providers.form.oauthCompleteBrowser') }}
                  </v-btn>
                </div>
              </div>
              <div v-if="oauthFlow && oauthFlow.providerId === formData.provider_id" class="oauth-box">
                <div><strong>Verification URL:</strong> {{ oauthFlow.verificationUrl || '-' }}</div>
                <div><strong>User code:</strong> {{ oauthFlow.userCode || '-' }}</div>
                <div><strong>Flow status:</strong> {{ oauthFlow.status }}</div>
                <div v-if="oauthFlow.message">{{ oauthFlow.message }}</div>
              </div>
              <div v-if="oauthError" class="text-error">{{ oauthError }}</div>
            </div>
          </div>

          <v-text-field
            v-model.number="formData.concurrency"
            :label="t('providers.form.concurrency')"
            type="number"
            :min="0"
            hint="0 = unlimited"
            persistent-hint />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="closeForm">{{ t('providers.form.cancel') }}</v-btn>
          <v-btn variant="text" @click="saveAndContinue" :disabled="!canSaveAndContinue">
            {{ t('providers.form.saveAndContinue') }}
          </v-btn>
          <v-btn color="primary" @click="submitForm">
            {{ isNew ? t('providers.form.create') : t('providers.form.save') }}
          </v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </v-card>
</template>

<style scoped>
.panel {
  min-height: 320px;
}

.oauth-box {
  border: 1px solid rgba(0, 0, 0, 0.12);
  border-radius: 8px;
  padding: 8px;
  background: rgba(0, 0, 0, 0.02);
  word-break: break-word;
}
</style>
