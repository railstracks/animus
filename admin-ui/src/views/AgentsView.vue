<script setup lang="ts">
import { ref, onMounted, computed, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { apiGet, apiRequest } from '../lib/api';

const { t } = useI18n();

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

interface ToolDef {
  name: string;
  description: string;
  config_parameters?: Array<{
    name: string;
    type: string;
    description: string;
    required?: boolean;
    default?: unknown;
  }>;
}

interface AgentModelConfig {
  provider: string;
  model_id: string;
}

interface AgentReasoningConfig {
  enabled: boolean;
  effort: string;
}

interface Agent {
  id: string;
  name: string;
  description: string;
  identity: string;
  avatar: string;
  model: AgentModelConfig;
  temperature: number;
  reasoning: AgentReasoningConfig;
  budget: {
    max_chain_steps: number;
    max_tool_calls_per_chain: number;
    timeout_seconds: number;
    token_budget_per_prompt: number;
    episodic_token_budget: number;
    semantic_token_budget: number;
    perspectives_token_budget: number;
  memory_file_token_budget: number;
  ambient_context_limit: number;
  session_report_token_budget: number;
    consolidation_tool_budget: number;
  };
  enabled_tools: string[];
  allowed_nodes?: string[];
  tool_configs?: Record<string, unknown>;
  default_vision_model?: string;
  created_at_unix_ms: number;
  updated_at_unix_ms: number;
}

interface ProviderInfo {
  provider_id: string;
  default_model: string;
}

interface ProvidersResponse {
  default_provider: string;
  providers: ProviderInfo[];
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const agents = ref<Agent[]>([]);
const tools = ref<ToolDef[]>([]);
const providers = ref<ProviderInfo[]>([]);
const defaultProviderId = ref('');
const availableNodes = ref<string[]>([]);
const modelsForProvider = ref<string[]>([]);
const modelsLoading = ref(false);
const visionModels = ref<{provider_id: string; model_id: string; label: string}[]>([]);
const loading = ref(true);
const error = ref('');
const successMsg = ref('');
const showForm = ref(false);
const editingAgent = ref<Agent | null>(null);
const formTab = ref('identity');

const formData = ref({
  id: '',
  name: '',
  description: '',
  identity: '',
  avatar: '',
  model_provider: '',
  model_id: '',
  default_vision_model: '',
  temperature: 0.7,
  reasoning_enabled: false,
  reasoning_effort: 'high',
  pad_context: true,
  max_chain_steps: 200,
  max_tool_calls_per_chain: 10,
  timeout_seconds: 1800,
  token_budget_per_prompt: 200000,
    episodic_token_budget: 10000,
    semantic_token_budget: 10000,
    perspectives_token_budget: 3000,
    memory_file_token_budget: 2500,
  session_report_token_budget: 1500,
    ambient_context_limit: 5000,
    consolidation_tool_budget: 30,
    episodic_token_budget: 10000,
    semantic_token_budget: 10000,
    perspectives_token_budget: 3000,
    memory_file_token_budget: 2500,
  session_report_token_budget: 1500,
    ambient_context_limit: 5000,
    consolidation_tool_budget: 30,
  enabled_tools: [] as string[],
  allowed_nodes: [] as string[],
  tool_configs: {} as Record<string, unknown>,
});

const isNew = computed(() => !editingAgent.value);

const visionModelOptions = computed(() => [
  { label: 'None', value: '' },
  ...visionModels.value.map((m) => ({
    label: m.label,
    value: `${m.provider_id}/${m.model_id}`,
  })),
]);

interface FileToolConfig {
  restrict_to_workspace: boolean;
  workspace_root: string;
  path_allowlist: string[];
  path_denylist: string[];
}

function defaultFileToolConfig(): FileToolConfig {
  return {
    restrict_to_workspace: true,
    workspace_root: '',
    path_allowlist: [],
    path_denylist: []
  };
}

function ensureFileToolConfig(): FileToolConfig {
  const root = formData.value.tool_configs as Record<string, unknown>;
  const existing = root.file as Partial<FileToolConfig> | undefined;
  if (!existing || typeof existing !== 'object') {
    const created = defaultFileToolConfig();
    root.file = created;
    return created;
  }
  if (!Array.isArray(existing.path_allowlist)) existing.path_allowlist = [];
  if (!Array.isArray(existing.path_denylist)) existing.path_denylist = [];
  if (typeof existing.workspace_root !== 'string') existing.workspace_root = '';
  if (typeof existing.restrict_to_workspace !== 'boolean') existing.restrict_to_workspace = true;
  return existing as FileToolConfig;
}

const fileToolConfig = computed<FileToolConfig>(() => ensureFileToolConfig());

// ---------------------------------------------------------------------------
// Data loading
// ---------------------------------------------------------------------------

async function loadAgents() {
  loading.value = true;
  error.value = '';
  try {
    const data = await apiGet<{ agents: Agent[] }>('/api/v1/agents');
    agents.value = data.agents;
  } catch (e: any) {
    error.value = e.message || t('agents.errors.loadFailed');
  } finally {
    loading.value = false;
  }
}

async function loadTools() {
  try {
    const data = await apiGet<{ tools: ToolDef[] }>('/api/v1/tools');
    tools.value = data.tools;
  } catch (_e: any) {
    // Non-critical — tool checkbox grid just won't show options
  }
}

async function loadProviders() {
  try {
    const data = await apiGet<ProvidersResponse>('/api/v1/providers');
    providers.value = Array.isArray(data.providers) ? data.providers : [];
    defaultProviderId.value = data.default_provider || '';
  } catch (_e: any) {
    providers.value = [];
    defaultProviderId.value = '';
  }
}

async function loadVisionModels() {
  try {
    const data = await apiGet<{ models?: {provider_id: string; model_id: string; label: string}[] }>('/api/v1/vision-models');
    visionModels.value = Array.isArray(data.models) ? data.models : [];
  } catch (_e: any) {
    visionModels.value = [];
  }
}

async function loadNodes() {
  try {
    const data = await apiGet<{ nodes: { name: string }[] }>('/api/v1/nodes');
    availableNodes.value = (data.nodes ?? []).map(n => n.name);
  } catch (_e: any) {
    availableNodes.value = [];
  }
}

async function loadModelsForProvider(providerId: string, keepModel = '') {
  modelsForProvider.value = [];
  if (!providerId) return;
  modelsLoading.value = true;
  try {
    const data = await apiGet<{ models?: string[] }>(`/api/v1/providers/${providerId}/models`);
    const models = Array.isArray(data.models) ? [...data.models].sort() : [];
    if (keepModel && keepModel.length > 0 && !models.includes(keepModel)) {
      models.unshift(keepModel);
    }
    modelsForProvider.value = models;
  } catch (_e: any) {
    if (keepModel) {
      modelsForProvider.value = [keepModel];
    }
  } finally {
    modelsLoading.value = false;
  }
}

// ---------------------------------------------------------------------------
// CRUD
// ---------------------------------------------------------------------------

function openCreate() {
  editingAgent.value = null;
  formTab.value = 'identity';
  formData.value = {
    id: '',
    name: '',
    description: '',
    identity: '',
    avatar: '',
    model_provider: defaultProviderId.value || providers.value[0]?.provider_id || '',
    model_id: '',
    temperature: 0.7,
    reasoning_enabled: false,
    reasoning_effort: 'high',
    pad_context: true,
    max_chain_steps: 200,
    max_tool_calls_per_chain: 10,
    timeout_seconds: 1800,
    token_budget_per_prompt: 200000,
    enabled_tools: [],
    allowed_nodes: [],
    tool_configs: {},
  };
  const provider = providers.value.find((p) => p.provider_id === formData.value.model_provider);
  if (provider?.default_model) {
    formData.value.model_id = provider.default_model;
  }
  void loadModelsForProvider(formData.value.model_provider, formData.value.model_id);
  showForm.value = true;
}

function openEdit(a: Agent) {
  editingAgent.value = a;
  formTab.value = 'identity';
  formData.value = {
    id: a.id,
    name: a.name,
    description: a.description,
    identity: a.identity,
    avatar: a.avatar,
    model_provider: a.model?.provider || '',
    model_id: a.model?.model_id || '',
    default_vision_model: (a as any).default_vision_model || '',
    temperature: a.temperature,
    reasoning_enabled: a.reasoning?.enabled || false,
    reasoning_effort: a.reasoning?.effort || 'high',
    pad_context: a.pad_context ?? true,
    max_chain_steps: a.budget.max_chain_steps,
    max_tool_calls_per_chain: a.budget.max_tool_calls_per_chain,
    timeout_seconds: a.budget.timeout_seconds,
    token_budget_per_prompt: a.budget.token_budget_per_prompt,
    episodic_token_budget: a.budget.episodic_token_budget,
    semantic_token_budget: a.budget.semantic_token_budget,
    perspectives_token_budget: a.budget.perspectives_token_budget,
    memory_file_token_budget: a.budget.memory_file_token_budget,
    ambient_context_limit: a.budget.ambient_context_limit,
    session_report_token_budget: a.budget.session_report_token_budget,
    consolidation_tool_budget: a.budget.consolidation_tool_budget,
    enabled_tools: [...a.enabled_tools],
    allowed_nodes: [...(a.allowed_nodes || [])],
    tool_configs: JSON.parse(JSON.stringify(a.tool_configs || {})),
  };
  void loadModelsForProvider(formData.value.model_provider, formData.value.model_id);
  showForm.value = true;
}

function closeForm() {
  showForm.value = false;
  editingAgent.value = null;
}

async function submitForm() {
  try {
    if (!formData.value.model_provider || !formData.value.model_id) {
      error.value = t('agents.errors.providerModelRequired');
      return;
    }

    const payload: Record<string, unknown> = {
      name: formData.value.name,
      description: formData.value.description,
      identity: formData.value.identity,
      avatar: formData.value.avatar,
      model: {
        provider: formData.value.model_provider,
        model_id: formData.value.model_id,
      },
      temperature: Number(formData.value.temperature),
      reasoning: {
        enabled: formData.value.reasoning_enabled,
        effort: formData.value.reasoning_effort,
      },
      pad_context: formData.value.pad_context,
      budget: {
        max_chain_steps: Number(formData.value.max_chain_steps),
        max_tool_calls_per_chain: Number(formData.value.max_tool_calls_per_chain),
        timeout_seconds: Number(formData.value.timeout_seconds),
        token_budget_per_prompt: Number(formData.value.token_budget_per_prompt),
        episodic_token_budget: Number(formData.value.episodic_token_budget),
        semantic_token_budget: Number(formData.value.semantic_token_budget),
        perspectives_token_budget: Number(formData.value.perspectives_token_budget),
        memory_file_token_budget: Number(formData.value.memory_file_token_budget),
        ambient_context_limit: Number(formData.value.ambient_context_limit),
        session_report_token_budget: Number(formData.value.session_report_token_budget),
        consolidation_tool_budget: Number(formData.value.consolidation_tool_budget),
      },
      enabled_tools: formData.value.enabled_tools,
      allowed_nodes: formData.value.allowed_nodes,
      tool_configs: formData.value.tool_configs,
      default_vision_model: formData.value.default_vision_model || '',
    };

    let savedId = formData.value.id;
    if (isNew.value) {
      const created = await apiRequest<Agent>('POST', '/api/v1/agents', payload);
      savedId = created.id || savedId;
    } else {
      await apiRequest('PATCH', `/api/v1/agents/${formData.value.id}`, payload);
    }
    closeForm();
    await loadAgents();
    successMsg.value = isNew.value
      ? t('agents.createSuccess', { id: savedId })
      : t('agents.updateSuccess', { id: savedId });
  } catch (e: any) {
    error.value = e.message;
  }
}

async function deleteAgent(id: string) {
  if (id === 'default') return;
  if (!confirm(t('agents.actions.confirmDelete', { id }))) return;
  try {
    await apiRequest('DELETE', `/api/v1/agents/${id}`);
    await loadAgents();
    successMsg.value = t('agents.deleteSuccess', { id });
  } catch (e: any) {
    error.value = e.message;
  }
}

// ---------------------------------------------------------------------------
// Tool toggling
// ---------------------------------------------------------------------------

function isToolEnabled(toolName: string): boolean {
  return formData.value.enabled_tools.includes(toolName);
}

function toggleTool(toolName: string) {
  const idx = formData.value.enabled_tools.indexOf(toolName);
  if (idx >= 0) {
    formData.value.enabled_tools.splice(idx, 1);
  } else {
    formData.value.enabled_tools.push(toolName);
  }
}

function enableAllTools() {
  formData.value.enabled_tools = tools.value.map(t => t.name);
}

function clearTools() {
  formData.value.enabled_tools = [];
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function formatDate(unixMs: number): string {
  if (!unixMs) return '—';
  return new Date(unixMs).toLocaleString();
}

function toolCount(a: Agent): string {
  if (a.enabled_tools.length === 0) return t('agents.toolCountAll');
  return String(a.enabled_tools.length);
}

onMounted(() => {
  void Promise.all([loadAgents(), loadTools(), loadProviders(), loadNodes(), loadVisionModels()]);
});

watch(
  () => formData.value.model_provider,
  async (providerId, prevProviderId) => {
    if (!showForm.value || providerId === prevProviderId) return;
    const provider = providers.value.find((p) => p.provider_id === providerId);
    const keepModel = formData.value.model_id;
    if (!keepModel && provider?.default_model) {
      formData.value.model_id = provider.default_model;
    }
    await loadModelsForProvider(providerId, formData.value.model_id);
    if (
      formData.value.model_id &&
      modelsForProvider.value.length > 0 &&
      !modelsForProvider.value.includes(formData.value.model_id)
    ) {
      formData.value.model_id = provider?.default_model || modelsForProvider.value[0] || '';
    }
  }
);
</script>

<template>
  <v-card rounded="xl" class="panel">
    <v-card-title class="d-flex align-center justify-space-between">
      <span>{{ t('agents.title') }}</span>
      <div>
        <v-btn variant="text" size="small" @click="loadAgents" :loading="loading">
          {{ t('agents.actions.refresh') }}
        </v-btn>
        <v-btn color="primary" size="small" @click="openCreate">
          {{ t('agents.actions.add') }}
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

      <v-progress-linear v-if="loading" indeterminate class="mb-4" />

      <v-table v-if="!loading && agents.length" density="comfortable">
        <thead>
          <tr>
            <th>{{ t('agents.columns.name') }}</th>
            <th>{{ t('agents.columns.provider') }}</th>
            <th>{{ t('agents.columns.model') }}</th>
            <th>{{ t('agents.columns.reasoning') }}</th>
            <th>{{ t('agents.columns.tools') }}</th>
            <th>{{ t('agents.columns.updated') }}</th>
            <th>{{ t('agents.columns.actions') }}</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="a in agents" :key="a.id">
            <td>
              <div class="d-flex align-center ga-2">
                <strong>{{ a.name || a.id }}</strong>
                <v-chip v-if="a.id === 'default'" size="x-small" color="primary" variant="tonal">
                  {{ t('agents.defaultBadge') }}
                </v-chip>
              </div>
              <div v-if="a.description" class="text-caption text-medium-emphasis">{{ a.description }}</div>
            </td>
            <td>{{ a.model?.provider || '—' }}</td>
            <td>{{ a.model?.model_id || '—' }}</td>
            <td>
              <v-chip v-if="a.reasoning?.enabled" size="small" color="success" variant="tonal">
                {{ a.reasoning?.effort }}
              </v-chip>
              <span v-else class="text-medium-emphasis">{{ t('agents.reasoningOff') }}</span>
            </td>
            <td>{{ toolCount(a) }}</td>
            <td class="text-caption">{{ formatDate(a.updated_at_unix_ms) }}</td>
            <td>
              <v-btn variant="text" size="x-small" @click="openEdit(a)">
                {{ t('agents.actions.edit') }}
              </v-btn>
              <v-btn
                v-if="a.id !== 'default'"
                variant="text" size="x-small" color="error"
                @click="deleteAgent(a.id)"
              >
                {{ t('agents.actions.delete') }}
              </v-btn>
              <v-tooltip v-else activator="parent" location="bottom">
                {{ t('agents.cannotDeleteDefault') }}
              </v-tooltip>
            </td>
          </tr>
        </tbody>
      </v-table>

      <v-empty-state v-if="!loading && !agents.length"
        :title="t('agents.empty.title')"
        :text="t('agents.empty.description')"
      />
    </v-card-text>

    <!-- Agent Create/Edit Dialog -->
    <v-dialog v-model="showForm" max-width="720" scrollable>
      <v-card>
        <v-card-title>
          {{ isNew ? t('agents.form.createTitle') : t('agents.form.editTitle', { id: formData.id }) }}
        </v-card-title>

        <v-card-text>
          <v-tabs v-model="formTab" class="mb-4">
            <v-tab value="identity">{{ t('agents.form.tabs.identity') }}</v-tab>
            <v-tab value="model">{{ t('agents.form.tabs.model') }}</v-tab>
            <v-tab value="reasoning">{{ t('agents.form.tabs.reasoning') }}</v-tab>
            <v-tab value="budget">{{ t('agents.form.tabs.budget') }}</v-tab>
            <v-tab value="tools">{{ t('agents.form.tabs.tools') }}</v-tab>
          </v-tabs>

          <v-tabs-window v-model="formTab">
            <!-- Identity -->
            <v-tabs-window-item value="identity">
              <v-text-field
                v-model="formData.id"
                :label="t('agents.form.id')"
                disabled
                :hint="isNew ? t('agents.form.idAutoHint') : t('agents.form.idHint')"
                persistent-hint
              />
              <v-text-field v-model="formData.name"
                :label="t('agents.form.name')"
              />
              <v-text-field v-model="formData.description"
                :label="t('agents.form.description')"
              />
              <v-textarea v-model="formData.identity"
                :label="t('agents.form.systemPrompt')"
                rows="4"
                auto-grow
              />
              <v-text-field v-model="formData.avatar"
                :label="t('agents.form.avatar')"
                :hint="t('agents.form.avatarHint')"
                persistent-hint
              />
            </v-tabs-window-item>

            <!-- Model -->
            <v-tabs-window-item value="model">
              <v-select v-model="formData.model_provider"
                :label="t('agents.form.provider')"
                :hint="t('agents.form.providerHint')"
                :items="providers.map((p) => p.provider_id)"
                persistent-hint
              />
              <v-combobox v-model="formData.model_id"
                :label="t('agents.form.modelId')"
                :items="modelsForProvider"
                :loading="modelsLoading"
                :hint="t('agents.form.modelHint')"
                persistent-hint
              />
              <v-slider v-model="formData.temperature"
                :label="t('agents.form.temperature')"
                :min="0" :max="2" :step="0.05"
                thumb-label
              />
              <v-select v-model="formData.default_vision_model"
                :label="t('agents.form.visionModel')"
                :hint="t('agents.form.visionModelHint')"
                :items="visionModelOptions"
                item-title="label"
                item-value="value"
                persistent-hint
                clearable
              />
            </v-tabs-window-item>

            <!-- Reasoning -->
            <v-tabs-window-item value="reasoning">
              <v-switch v-model="formData.reasoning_enabled"
                :label="t('agents.form.reasoningEnabled')"
                color="primary"
              />
              <v-select v-model="formData.reasoning_effort"
                :label="t('agents.form.reasoningEffort')"
                :items="['low', 'medium', 'high', 'xhigh']"
                :disabled="!formData.reasoning_enabled"
              />
              <v-switch v-model="formData.pad_context"
                :label="t('agents.form.padContext')"
                color="primary"
                class="mt-4"
              />
            </v-tabs-window-item>

            <!-- Budget -->
            <v-tabs-window-item value="budget">
              <v-text-field v-model="formData.max_chain_steps"
                :label="t('agents.form.maxChainSteps')"
                type="number"
              />
              <v-text-field v-model="formData.max_tool_calls_per_chain"
                :label="t('agents.form.maxToolCallsPerChain')"
                type="number"
              />
              <v-text-field v-model="formData.timeout_seconds"
                :label="t('agents.form.timeoutSeconds')"
                type="number"
              />
              <v-text-field v-model="formData.token_budget_per_prompt"
                :label="t('agents.form.tokenBudgetPerPrompt')"
                type="number"
              />
              <v-text-field v-model="formData.episodic_token_budget"
                :label="t('agents.form.episodicTokenBudget')"
                type="number"
              />
              <v-text-field v-model="formData.semantic_token_budget"
                :label="t('agents.form.semanticTokenBudget')"
                type="number"
              />
              <v-text-field v-model="formData.perspectives_token_budget"
                :label="t('agents.form.perspectivesTokenBudget')"
                type="number"
              />
              <v-text-field v-model="formData.memory_file_token_budget"
                :label="t('agents.form.memoryFileTokenBudget')"
                type="number"
              />
              <v-text-field v-model="formData.ambient_context_limit"
                :label="t('agents.form.ambientContextLimit')"
                type="number"
              />
              <v-text-field v-model="formData.session_report_token_budget"
                label="Session Report Token Budget"
                type="number"
                hint="Max tokens for session reports in active memory"
                persistent-hint
              />
              <v-text-field v-model="formData.consolidation_tool_budget"
                :label="t('agents.form.consolidationToolBudget')"
                type="number"
              />
            </v-tabs-window-item>

            <!-- Tools -->
            <v-tabs-window-item value="tools">
              <p class="text-caption text-medium-emphasis mb-2">
                {{ t('agents.form.toolsHint') }}
              </p>
              <div class="d-flex ga-2 mb-3">
                <v-btn size="x-small" variant="tonal" @click="enableAllTools">
                  {{ t('agents.form.enableAll') }}
                </v-btn>
                <v-btn size="x-small" variant="tonal" @click="clearTools">
                  {{ t('agents.form.clearAll') }}
                </v-btn>
              </div>
              <v-checkbox v-for="tool in tools" :key="tool.name"
                :model-value="isToolEnabled(tool.name)"
                @update:model-value="toggleTool(tool.name)"
                density="compact"
                hide-details
              >
                <template #label>
                  <div>
                    <strong>{{ tool.name }}</strong>
                    <span class="text-medium-emphasis ml-2">{{ tool.description }}</span>
                  </div>
                </template>
              </v-checkbox>
              <p v-if="tools.length === 0" class="text-caption text-medium-emphasis">
                {{ t('agents.form.noToolsLoaded') }}
              </p>

              <v-card
                v-if="isToolEnabled('file')"
                variant="tonal"
                class="mt-4 pa-3"
              >
                <div class="text-subtitle-2 mb-2">{{ t('agents.form.fileToolConfigTitle') }}</div>
                <v-switch
                  v-model="fileToolConfig.restrict_to_workspace"
                  :label="t('agents.form.fileRestrictToWorkspace')"
                  density="compact"
                  hide-details
                  color="primary"
                />
                <v-text-field
                  v-model="fileToolConfig.workspace_root"
                  :label="t('agents.form.fileWorkspaceRoot')"
                  :hint="t('agents.form.fileWorkspaceRootHint')"
                  persistent-hint
                  density="compact"
                  class="mt-2"
                />
                <v-combobox
                  v-model="fileToolConfig.path_allowlist"
                  :label="t('agents.form.filePathAllowlist')"
                  :hint="t('agents.form.filePathAllowlistHint')"
                  persistent-hint
                  multiple
                  chips
                  closable-chips
                  clearable
                  density="compact"
                  class="mt-2"
                />
                <v-combobox
                  v-model="fileToolConfig.path_denylist"
                  :label="t('agents.form.filePathDenylist')"
                  :hint="t('agents.form.filePathDenylistHint')"
                  persistent-hint
                  multiple
                  chips
                  closable-chips
                  clearable
                  density="compact"
                  class="mt-2"
                />
              </v-card>

              <v-divider class="my-4" />

              <div class="text-subtitle-2 mb-1">Allowed Nodes</div>
              <p class="text-caption text-medium-emphasis mb-2">
                Restrict which connected nodes this agent can execute tools on.
                Leave empty to allow all nodes.
              </p>
              <v-combobox
                v-model="formData.allowed_nodes"
                :items="availableNodes"
                label="Allowed Nodes"
                hint="Type node names or select from connected nodes"
                persistent-hint
                multiple
                chips
                closable-chips
                clearable
                density="compact"
              />
            </v-tabs-window-item>
          </v-tabs-window>
        </v-card-text>

        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="closeForm">
            {{ t('agents.form.cancel') }}
          </v-btn>
          <v-btn color="primary" @click="submitForm">
            {{ isNew ? t('agents.form.create') : t('agents.form.save') }}
          </v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </v-card>
</template>

<style scoped>
.panel {
  max-width: 1200px;
  margin: 0 auto;
}
</style>
