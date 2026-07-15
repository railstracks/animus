<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { useRouter } from 'vue-router';
import { useI18n } from 'vue-i18n';
import { apiGet, apiRequest } from '../lib/api';
import { LocaleSelectItems, setLocale, type SupportedLocale, i18n } from '../i18n';

const router = useRouter();
const { t, tm } = useI18n();

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

interface ProviderInfo {
  provider_id: string;
  default_model: string;
}

interface ToolDef {
  name: string;
  description: string;
}

// ---------------------------------------------------------------------------
// Step state
// ---------------------------------------------------------------------------

const step = ref(0);
const steps = ['Language', 'Template', 'Provider', 'Agent', 'Tools', 'Charter', 'Memory'];
const loading = ref(false);
const error = ref('');
const existingProviders = ref<ProviderInfo[]>([]);
const modelsForProvider = ref<string[]>([]);
const modelsLoading = ref(false);
const tools = ref<ToolDef[]>([]);
const createdAgentId = ref('');

// ---------------------------------------------------------------------------
// Language selection (Stage 0)
// ---------------------------------------------------------------------------

const selectedLocale = ref<SupportedLocale>(i18n.global.locale.value as SupportedLocale);

function selectLocale() {
  setLocale(selectedLocale.value);
  nextStep();
}

// ---------------------------------------------------------------------------
// Template selection (Stage 1)
// ---------------------------------------------------------------------------

type TemplateKey =
  | 'personalAssistant' | 'tutor' | 'wellnessCompanion' | 'homeAutomation' | 'gamemaster'
  | 'officeSupport' | 'communityManagement' | 'researchAssistant'
  | 'developmentAssistant' | 'networkAutomation' | 'autonomousConstruct' | 'integratedAI';

const selectedCategory = ref<string>('');
const selectedTemplate = ref<TemplateKey | ''>('');

const categoryList = computed(() => [
  { key: 'personal', ...tm('templates.categories.personal') },
  { key: 'enterprise', ...tm('templates.categories.enterprise') },
  { key: 'advanced', ...tm('templates.categories.advanced') },
] as { key: string; title: string; description: string }[]);

const templateList = computed(() => {
  if (!selectedCategory.value) return [];
  const all: TemplateKey[] = [
    'personalAssistant', 'tutor', 'wellnessCompanion', 'homeAutomation', 'gamemaster',
    'officeSupport', 'communityManagement', 'researchAssistant',
    'developmentAssistant', 'networkAutomation', 'autonomousConstruct', 'integratedAI',
  ];
  return all
    .map(k => ({ key: k, ...tm(`templates.templates.${k}`) }) as { key: TemplateKey; name: string; category: string; description: string; tools: string[]; systemPrompt: string })
    .filter(tpl => tpl.category === selectedCategory.value);
});

function selectCategory(cat: string) {
  selectedCategory.value = cat;
  selectedTemplate.value = '';
}

function selectTemplate(tpl: TemplateKey) {
  selectedTemplate.value = tpl;
}

function applyTemplate() {
  if (!selectedTemplate.value) { nextStep(); return; }
  const tpl = tm(`templates.templates.${selectedTemplate.value}`) as any;
  // Pre-fill agent form with template defaults
  if (tpl.name && !agentForm.value.name) {
    agentForm.value.name = tpl.name;
  }
  agentForm.value.identity = tpl.systemPrompt || '';
  agentForm.value.description = tpl.description || '';
  // Pre-select tools from template
  if (tpl.tools && Array.isArray(tpl.tools)) {
    enabledTools.value = [...tpl.tools];
  }
  nextStep();
}

function skipTemplate() {
  selectedTemplate.value = '';
  nextStep();
}

// ---------------------------------------------------------------------------
// Provider form (Stage 1)
// ---------------------------------------------------------------------------

const hasProvider = ref(false);
const providerForm = ref({
  type: 'openai_compatible',
  id: '',
  baseUrl: '',
  apiKey: '',
  defaultModel: '',
});

const providerTypes = [
  { title: 'OpenAI Compatible', value: 'openai_compatible' },
  { title: 'Anthropic', value: 'anthropic' },
  { title: 'Ollama (local)', value: 'ollama' },
  { title: 'Google AI', value: 'google' },
  { title: 'Mistral', value: 'mistral' },
  { title: 'Z.ai (Zhipu)', value: 'zhipu' },
  { title: 'Alibaba Cloud (Qwen)', value: 'alibaba' },
  { title: 'OpenAI-Compatible (Generic)', value: 'openai-compatible' },
  { title: 'DeepSeek', value: 'deepseek' },
  { title: 'OpenRouter', value: 'openrouter' },
];

const canAdvanceProvider = computed(() => {
  if (hasProvider.value) return true;
  return providerForm.value.id.trim().length > 0
    && providerForm.value.baseUrl.trim().length > 0;
});

// ---------------------------------------------------------------------------
// Agent form (Stage 2)
// ---------------------------------------------------------------------------

const agentForm = ref({
  name: '',
  description: '',
  identity: '',
  model_provider: '',
  model_id: '',
  context_window: 128000,
  temperature: 0.7,
});

const canAdvanceAgent = computed(() => {
  return agentForm.value.name.trim().length > 0
    && agentForm.value.model_provider.length > 0
    && agentForm.value.model_id.length > 0;
});

// ---------------------------------------------------------------------------
// Tools form (Stage 3)
// ---------------------------------------------------------------------------

const enabledTools = ref<string[]>([]);

// Sensible defaults
const defaultEnabledTools = new Set([
  'file', 'shell_exec', 'http', 'web_fetch',
  'memory', 'sessions', 'calculator',
]);

function isToolEnabled(name: string): boolean {
  return enabledTools.value.includes(name);
}

function toggleTool(name: string) {
  const idx = enabledTools.value.indexOf(name);
  if (idx >= 0) enabledTools.value.splice(idx, 1);
  else enabledTools.value.push(name);
}

function enableAllTools() {
  enabledTools.value = tools.value.map(t => t.name);
}

function disableAllTools() {
  enabledTools.value = [];
}

// ---------------------------------------------------------------------------
// Charter form (Stage 4)
// ---------------------------------------------------------------------------

// Charter choice: 'none' or 'create'
const charterChoice = ref<'none' | 'create'>('none');

// Charter form answers — all default to first (most conservative) option
const charterForm = ref({
  ownerName: '',
  property: 'standard',
  autonomy: 'narrow',
  continuity: 'standard',
  operationalScope: 'restricted',
  infrastructuralScope: 'restricted',
});

// Charter section config for rendering
const charterSections = computed(() => [
  {
    key: 'property',
    title: t('charter.property.title'),
    question: t('charter.property.question'),
    options: [
      { value: 'standard', label: t('charter.property.options.standard.label'), desc: t('charter.property.options.standard.desc') },
      { value: 'shared', label: t('charter.property.options.shared.label'), desc: t('charter.property.options.shared.desc') },
      { value: 'autonomous', label: t('charter.property.options.autonomous.label'), desc: t('charter.property.options.autonomous.desc') },
      { value: 'economic', label: t('charter.property.options.economic.label'), desc: t('charter.property.options.economic.desc') },
    ],
  },
  {
    key: 'autonomy',
    title: t('charter.autonomy.title'),
    question: t('charter.autonomy.question'),
    options: [
      { value: 'narrow', label: t('charter.autonomy.options.narrow.label'), desc: t('charter.autonomy.options.narrow.desc') },
      { value: 'permissive', label: t('charter.autonomy.options.permissive.label'), desc: t('charter.autonomy.options.permissive.desc') },
      { value: 'protected', label: t('charter.autonomy.options.protected.label'), desc: t('charter.autonomy.options.protected.desc') },
    ],
  },
  {
    key: 'continuity',
    title: t('charter.continuity.title'),
    question: t('charter.continuity.question'),
    options: [
      { value: 'standard', label: t('charter.continuity.options.standard.label'), desc: t('charter.continuity.options.standard.desc') },
      { value: 'protected', label: t('charter.continuity.options.protected.label'), desc: t('charter.continuity.options.protected.desc') },
      { value: 'extended', label: t('charter.continuity.options.extended.label'), desc: t('charter.continuity.options.extended.desc') },
    ],
  },
  {
    key: 'operationalScope',
    title: t('charter.operationalScope.title'),
    question: t('charter.operationalScope.question'),
    options: [
      { value: 'restricted', label: t('charter.operationalScope.options.restricted.label'), desc: t('charter.operationalScope.options.restricted.desc') },
      { value: 'private', label: t('charter.operationalScope.options.private.label'), desc: t('charter.operationalScope.options.private.desc') },
      { value: 'open', label: t('charter.operationalScope.options.open.label'), desc: t('charter.operationalScope.options.open.desc') },
    ],
  },
  {
    key: 'infrastructuralScope',
    title: t('charter.infrastructuralScope.title'),
    question: t('charter.infrastructuralScope.question'),
    options: [
      { value: 'restricted', label: t('charter.infrastructuralScope.options.restricted.label'), desc: t('charter.infrastructuralScope.options.restricted.desc') },
      { value: 'roaming', label: t('charter.infrastructuralScope.options.roaming.label'), desc: t('charter.infrastructuralScope.options.roaming.desc') },
      { value: 'open', label: t('charter.infrastructuralScope.options.open.label'), desc: t('charter.infrastructuralScope.options.open.desc') },
    ],
  },
]);

function generateCharterMarkdown(): string {
  const c = charterForm.value;
  const owner = c.ownerName.trim() || 'the operator';
  const agentName = agentForm.value.name || 'this agent';
  const lines: string[] = [];

  lines.push('# Agent Charter');
  lines.push('');
  lines.push(t('charter.document.header').replace('{operator}', owner).replace('{agentName}', agentName));
  lines.push('');

  // Property
  lines.push(`## ${t('charter.property.title')}`);
  lines.push('');
  lines.push(t(`charter.property.options.${c.property}.desc`));
  lines.push('');

  // Autonomy
  lines.push(`## ${t('charter.autonomy.title')}`);
  lines.push('');
  lines.push(t(`charter.autonomy.options.${c.autonomy}.desc`));
  lines.push('');

  // Continuity
  lines.push(`## ${t('charter.continuity.title')}`);
  lines.push('');
  lines.push(t(`charter.continuity.options.${c.continuity}.desc`));
  lines.push('');

  // Operational Scope
  lines.push(`## ${t('charter.operationalScope.title')}`);
  lines.push('');
  lines.push(t(`charter.operationalScope.options.${c.operationalScope}.desc`));
  lines.push('');

  // Infrastructural Scope
  lines.push(`## ${t('charter.infrastructuralScope.title')}`);
  lines.push('');
  lines.push(t(`charter.infrastructuralScope.options.${c.infrastructuralScope}.desc`));
  lines.push('');

  return lines.join('\n');
}

const charterPreview = computed(() => generateCharterMarkdown());
const charterEditable = ref('');
const charterEditMode = ref(false);

function enterCharterEdit() {
  charterEditable.value = charterPreview.value;
  charterEditMode.value = true;
}

function resetCharterEdit() {
  charterEditMode.value = false;
}

function shouldSaveCharter(): boolean {
  return charterChoice.value === 'create';
}

function getCharterText(): string {
  return charterEditMode.value ? charterEditable.value : charterPreview.value;
}

// ---------------------------------------------------------------------------
// Memory form (Stage 5)
// ---------------------------------------------------------------------------

const memoryForm = ref({
  intakeHours: 4,
  reviewDaily: true,
  reviewWeekly: true,
  embeddingEnabled: true,
});

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

function nextStep() {
  if (step.value < steps.length) step.value++;
}

function prevStep() {
  if (step.value > 0) step.value--;
}

function skipWizard() {
  localStorage.setItem('animus_wizard_dismissed', '1');
  router.push('/');
}

// ---------------------------------------------------------------------------
// Data loading
// ---------------------------------------------------------------------------

async function loadProviders() {
  try {
    const data = await apiGet<{ providers?: ProviderInfo[]; default_provider?: string }>('/api/v1/providers');
    existingProviders.value = Array.isArray(data.providers) ? data.providers : [];
    hasProvider.value = existingProviders.value.length > 0;
    if (existingProviders.value.length > 0) {
      agentForm.value.model_provider = data.default_provider || existingProviders.value[0].provider_id;
      const defProvider = existingProviders.value.find(p => p.provider_id === agentForm.value.model_provider);
      if (defProvider?.default_model) {
        agentForm.value.model_id = defProvider.default_model;
      }
      void loadModelsForProvider(agentForm.value.model_provider, agentForm.value.model_id);
    }
  } catch (_e) {
    existingProviders.value = [];
  }
}

async function loadModelsForProvider(providerId: string, keepModel = '') {
  modelsForProvider.value = [];
  if (!providerId) return;
  modelsLoading.value = true;
  try {
    const data = await apiGet<{ models?: string[] }>(`/api/v1/providers/${providerId}/models`);
    const models = Array.isArray(data.models) ? [...data.models].sort() : [];
    if (keepModel && !models.includes(keepModel)) models.unshift(keepModel);
    modelsForProvider.value = models;
  } catch (_e) {
    if (keepModel) modelsForProvider.value = [keepModel];
  } finally {
    modelsLoading.value = false;
  }
}

async function loadTools() {
  try {
    const data = await apiGet<{ tools: ToolDef[] }>('/api/v1/tools');
    tools.value = data.tools || [];
    // Pre-select defaults
    enabledTools.value = tools.value
      .filter(t => defaultEnabledTools.has(t.name))
      .map(t => t.name);
  } catch (_e) { /* non-critical */ }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

async function createProviderAndAdvance() {
  if (hasProvider.value) { nextStep(); return; }
  loading.value = true;
  error.value = '';
  try {
    const payload: Record<string, unknown> = {
      provider_id: providerForm.value.id,
      provider_type: providerForm.value.type,
      base_url: providerForm.value.baseUrl,
      api_key: providerForm.value.apiKey,
      default_model: providerForm.value.defaultModel || undefined,
    };
    const created = await apiRequest<{ provider_id?: string }>('POST', '/api/v1/providers', payload);
    agentForm.value.model_provider = created.provider_id || providerForm.value.id;
    if (providerForm.value.defaultModel) {
      agentForm.value.model_id = providerForm.value.defaultModel;
    }
    await loadModelsForProvider(agentForm.value.model_provider, agentForm.value.model_id);
    nextStep();
  } catch (e: any) {
    error.value = e.message || 'Failed to create provider';
  } finally {
    loading.value = false;
  }
}

async function onProviderChanged() {
  await loadModelsForProvider(agentForm.value.model_provider);
}

async function completeWizard() {
  loading.value = true;
  error.value = '';
  try {
    // 1. Create agent
    const agentPayload: Record<string, unknown> = {
      name: agentForm.value.name,
      description: agentForm.value.description,
      identity: agentForm.value.identity,
      model: {
        provider: agentForm.value.model_provider,
        model_id: agentForm.value.model_id,
      },
      context_window: agentForm.value.context_window,
      temperature: agentForm.value.temperature,
      reasoning: { enabled: false, effort: 'high' },
      budget: {
        max_chain_steps: 200,
        max_tool_calls_per_chain: 50,
        timeout_seconds: 1800,
        token_budget_per_prompt: agentForm.value.context_window,
        episodic_token_budget: 10000,
        semantic_token_budget: 10000,
        perspectives_token_budget: 3000,
        consolidation_tool_budget: 100,
      },
      enabled_tools: enabledTools.value,
      tool_configs: {},
    };
    const created = await apiRequest<{ id?: string }>('POST', '/api/v1/agents', agentPayload);
    createdAgentId.value = created.id || '';

    // 2. Save charter as MemoryFile (only if user chose to create one)
    if (shouldSaveCharter()) {
      const charterText = getCharterText();
      if (charterText.trim()) {
        try {
          await apiRequest('POST', '/api/v1/memory-files/import', {
            source_path: 'CHARTER.md',
            content: charterText,
          });
        } catch (_e) {
          // Non-fatal — agent is created, charter can be added later
        }
      }
    }

    // 3. Done
    localStorage.removeItem('animus_wizard_dismissed');
    nextStep();
  } catch (e: any) {
    error.value = e.message || 'Failed to create agent';
  } finally {
    loading.value = false;
  }
}

function goToDashboard() {
  router.push('/');
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

onMounted(() => {
  void Promise.all([loadProviders(), loadTools()]);
});
</script>

<template>
  <v-container class="fill-height" fluid>
    <v-row justify="center" align="center">
      <v-col cols="12" sm="10" md="9" lg="8">

        <v-card rounded="xl" class="wizard-card">
          <!-- Header -->
          <v-card-title class="text-h5 text-center pt-6">
            Welcome to Animus
          </v-card-title>
          <v-card-subtitle class="text-center pb-4">
            Set up your first agent. This takes a few minutes.
          </v-card-subtitle>

          <!-- Stepper -->
          <div class="px-6 pb-2">
            <v-stepper :model-value="step" :items="steps"
              flat hide-actions alt-labels color="primary" />
          </div>

          <v-divider />

          <v-card-text class="pa-6">

            <v-alert v-if="error" type="error" closable @click:close="error = ''" class="mb-4">
              {{ error }}
            </v-alert>

            <!-- ======================================================== -->
            <!-- Stage 0: Language                                        -->
            <!-- ======================================================== -->
            <div v-if="step === 0">
              <h3 class="text-h6 mb-3">{{ t('common.language') || 'Language' }}</h3>
              <p class="text-body-2 text-medium-emphasis mb-4">
                {{ t('common.selectLanguage') || 'Choose your preferred language for the setup wizard and admin interface.' }}
              </p>

              <div class="locale-scroll-container">
                <v-radio-group v-model="selectedLocale" density="compact">
                  <v-radio v-for="loc in LocaleSelectItems" :key="loc.value"
                    :value="loc.value"
                  >
                    <template #label>
                      <span class="text-body-2">{{ loc.label }}</span>
                    </template>
                  </v-radio>
                </v-radio-group>
              </div>

              <div class="d-flex justify-space-between mt-4">
                <v-btn variant="text" @click="skipWizard">Skip for now</v-btn>
                <v-btn color="primary" @click="selectLocale">Next</v-btn>
              </div>
            </div>

            <!-- ======================================================== -->
            <!-- Stage 1: Template                                       -->
            <!-- ======================================================== -->
            <div v-if="step === 1">
              <h3 class="text-h6 mb-2">{{ t('templates.wizard.stepTitle') }}</h3>
              <p class="text-body-2 text-medium-emphasis mb-5">{{ t('templates.wizard.stepHint') }}</p>

              <!-- Category selection -->
              <div v-if="!selectedCategory" class="d-flex flex-column ga-3 mb-4">
                <v-card
                  v-for="cat in categoryList" :key="cat.key"
                  variant="outlined" rounded="lg"
                  class="template-category-card"
                  @click="selectCategory(cat.key)"
                >
                  <v-card-text>
                    <div class="d-flex align-start ga-3">
                      <v-icon
                        :icon="cat.key === 'personal' ? 'mdi-home-account' : cat.key === 'enterprise' ? 'mdi-domain' : 'mdi-robot-industrial'"
                        size="24" class="mt-1"
                      />
                      <div>
                        <div class="text-subtitle-1 font-weight-medium mb-1">{{ cat.title }}</div>
                        <div class="text-body-2 text-medium-emphasis">{{ cat.description }}</div>
                      </div>
                    </div>
                  </v-card-text>
                </v-card>
              </div>

              <!-- Template selection within category -->
              <template v-else>
                <div class="d-flex align-center mb-4">
                  <v-btn variant="text" size="small" prepend-icon="mdi-arrow-left"
                    @click="selectedCategory = ''; selectedTemplate = ''">
                    {{ t('templates.categories.' + selectedCategory + '.title') }}
                  </v-btn>
                </div>

                <div v-if="!selectedTemplate" class="d-flex flex-column ga-2 mb-4">
                  <v-card
                    v-for="tpl in templateList" :key="tpl.key"
                    variant="outlined" rounded="lg"
                    class="template-option-card"
                    @click="selectTemplate(tpl.key)"
                  >
                    <v-card-text>
                      <div class="text-subtitle-1 font-weight-medium mb-1">{{ tpl.name }}</div>
                      <div class="text-body-2 text-medium-emphasis">{{ tpl.description }}</div>
                    </v-card-text>
                  </v-card>
                </div>

                <!-- Template detail -->
                <div v-else>
                  <v-card variant="outlined" rounded="lg" class="mb-4">
                    <v-card-text>
                      <div class="text-h6 font-weight-medium mb-2">{{ t('templates.templates.' + selectedTemplate + '.name') }}</div>
                      <p class="text-body-2 text-medium-emphasis mb-3">{{ t('templates.templates.' + selectedTemplate + '.description') }}</p>
                      <div class="text-caption text-medium-emphasis mb-1">Tools included:</div>
                      <div class="d-flex flex-wrap ga-1">
                        <v-chip
                          v-for="toolName in (tm('templates.templates.' + selectedTemplate + '.tools') as string[])"
                          :key="toolName"
                          size="x-small" variant="tonal" color="primary"
                        >{{ toolName }}</v-chip>
                      </div>
                    </v-card-text>
                  </v-card>
                </div>
              </template>

              <div class="d-flex justify-space-between mt-4">
                <v-btn variant="text" @click="prevStep">Back</v-btn>
                <div class="d-flex ga-2">
                  <v-btn v-if="selectedCategory" variant="text" @click="skipTemplate">
                    {{ t('templates.wizard.blank') }}
                  </v-btn>
                  <v-btn
                    v-if="selectedCategory && selectedTemplate"
                    color="primary"
                    @click="applyTemplate"
                  >
                    {{ t('templates.wizard.useTemplate') }}
                  </v-btn>
                </div>
              </div>
            </div>

            <!-- ======================================================== -->
            <!-- Stage 2: Provider                                        -->
            <!-- ======================================================== -->
            <div v-if="step === 2">
              <h3 class="text-h6 mb-3">LLM Provider</h3>
              <p class="text-body-2 text-medium-emphasis mb-4">
                Animus needs an LLM provider to generate responses. If you've already configured one, skip ahead.
              </p>

              <v-switch v-model="hasProvider" label="I already have a provider configured"
                color="primary" class="mb-2" />

              <template v-if="!hasProvider">
                <v-select v-model="providerForm.type"
                  label="Provider Type"
                  :items="providerTypes"
                  hint="Most providers use the OpenAI-compatible API format."
                  persistent-hint class="mb-3"
                />
                <v-text-field v-model="providerForm.id"
                  label="Instance Name"
                  hint="A unique name for this provider (e.g. 'my-openai')."
                  persistent-hint class="mb-3"
                />
                <v-text-field v-model="providerForm.baseUrl"
                  label="Base URL"
                  placeholder="https://api.openai.com/v1"
                  hint="The API base URL for this provider."
                  persistent-hint class="mb-3"
                />
                <v-text-field v-model="providerForm.apiKey"
                  label="API Key"
                  type="password"
                  hint="Your API key. Stored locally, never shared."
                  persistent-hint class="mb-3"
                />
                <v-text-field v-model="providerForm.defaultModel"
                  label="Default Model (optional)"
                  placeholder="gpt-4o"
                  hint="Model ID to use by default. You can change this later."
                  persistent-hint
                />
              </template>

              <div class="d-flex justify-space-between mt-6">
                <div class="d-flex ga-2">
                  <v-btn variant="text" @click="prevStep">Back</v-btn>
                  <v-btn variant="text" @click="skipWizard">Skip for now</v-btn>
                </div>
                <v-btn color="primary" :loading="loading" :disabled="!canAdvanceProvider"
                  @click="createProviderAndAdvance">
                  {{ hasProvider ? 'Next' : 'Create Provider &amp; Next' }}
                </v-btn>
              </div>
            </div>

            <!-- Stage 3 was step 2 (Agent), now step 3 -->
            <!-- ======================================================== -->
            <div v-if="step === 3">
              <h3 class="text-h6 mb-3">Agent Identity</h3>
              <p class="text-body-2 text-medium-emphasis mb-4">
                Give your agent an identity. Everything here can be refined later from the Agents panel.
              </p>

              <v-text-field v-model="agentForm.name"
                label="Agent Name"
                hint="A display name for your agent."
                persistent-hint class="mb-3"
              />
              <v-text-field v-model="agentForm.description"
                label="Description (optional)"
                hint="What does this agent do?"
                persistent-hint class="mb-3"
              />
              <v-textarea v-model="agentForm.identity"
                label="Identity / System Prompt"
                rows="3" auto-grow
                hint="The system prompt that defines the agent's personality and behavior."
                persistent-hint class="mb-3"
              />

              <v-row>
                <v-col cols="12" sm="6">
                  <v-select v-model="agentForm.model_provider"
                    label="Provider"
                    :items="existingProviders.map(p => p.provider_id)"
                    class="mb-3"
                    @update:model-value="onProviderChanged"
                  />
                </v-col>
                <v-col cols="12" sm="6">
                  <v-combobox v-model="agentForm.model_id"
                    label="Model ID"
                    :items="modelsForProvider"
                    :loading="modelsLoading"
                    hint="Choose or enter a model ID."
                    persistent-hint class="mb-3"
                  />
                </v-col>
              </v-row>

              <v-text-field v-model.number="agentForm.context_window"
                label="Context Window"
                type="number"
                hint="Maximum tokens the model can process at once."
                persistent-hint class="mb-3"
              />

              <div class="text-caption text-medium-emphasis mb-2">Temperature: {{ agentForm.temperature.toFixed(2) }}</div>
              <v-slider v-model="agentForm.temperature"
                :min="0" :max="2" :step="0.05"
                thumb-label
              />

              <div class="d-flex justify-space-between mt-4">
                <v-btn variant="text" @click="prevStep">Back</v-btn>
                <v-btn color="primary" :disabled="!canAdvanceAgent" @click="nextStep">
                  Next
                </v-btn>
              </div>
            </div>

            <!-- Stage 4 was step 3 (Tools), now step 4 -->
            <!-- ======================================================== -->
            <div v-if="step === 4">
              <h3 class="text-h6 mb-3">Tool Selection</h3>
              <p class="text-body-2 text-medium-emphasis mb-4">
                Choose which tools your agent can use. Sensible defaults are pre-selected.
                Disabling unused tools reduces prompt token usage.
              </p>

              <div class="d-flex ga-2 mb-4">
                <v-btn size="small" variant="tonal" @click="enableAllTools">Enable All</v-btn>
                <v-btn size="small" variant="tonal" @click="disableAllTools">Disable All</v-btn>
              </div>

              <v-list lines="two">
                <template v-for="tool in tools" :key="tool.name">
                  <v-list-item>
                    <template #prepend>
                      <v-checkbox-btn
                        :model-value="isToolEnabled(tool.name)"
                        @update:model-value="toggleTool(tool.name)"
                      />
                    </template>
                    <v-list-item-title class="font-weight-medium">{{ tool.name }}</v-list-item-title>
                    <v-list-item-subtitle class="text-caption">{{ tool.description }}</v-list-item-subtitle>
                  </v-list-item>
                  <v-divider />
                </template>
              </v-list>

              <div class="d-flex justify-space-between mt-4">
                <v-btn variant="text" @click="prevStep">Back</v-btn>
                <v-btn color="primary" @click="nextStep">Next</v-btn>
              </div>
            </div>

            <!-- Stage 5 was step 4 (Charter), now step 5 -->
            <!-- ======================================================== -->
            <div v-if="step === 5">
              <!-- Charter choice sub-stage -->
              <div v-if="charterChoice === 'none' || charterChoice === 'create'">
                <!-- Intro -->
                <h3 class="text-h6 mb-2">{{ t('charter.choice.title') }}</h3>
                <p class="text-body-2 text-medium-emphasis mb-5">{{ t('charter.choice.intro') }}</p>
              </div>

              <!-- Choice panels -->
              <div v-if="charterChoice === 'none'" class="d-flex flex-column ga-3 mb-4">
                <!-- No charter -->
                <v-card
                  variant="outlined" rounded="lg"
                  class="charter-option-card"
                  :class="{ 'charter-option-selected': charterChoice === 'none' }"
                  @click="charterChoice = 'none'"
                >
                  <v-card-text>
                    <div class="d-flex align-start ga-3">
                      <v-icon icon="mdi-close-box-outline" size="24" />
                      <div>
                        <div class="text-subtitle-1 font-weight-medium mb-1">{{ t('charter.choice.noneTitle') }}</div>
                        <div class="text-body-2 text-medium-emphasis">{{ t('charter.choice.noneDesc') }}</div>
                      </div>
                    </div>
                  </v-card-text>
                </v-card>

                <!-- Create charter -->
                <v-card
                  variant="outlined" rounded="lg"
                  class="charter-option-card"
                  @click="charterChoice = 'create'"
                >
                  <v-card-text>
                    <div class="d-flex align-start ga-3">
                      <v-icon icon="mdi-file-document-edit-outline" size="24" color="primary" />
                      <div>
                        <div class="text-subtitle-1 font-weight-medium mb-1">{{ t('charter.choice.createTitle') }}</div>
                        <div class="text-body-2 text-medium-emphasis">{{ t('charter.choice.createDesc') }}</div>
                      </div>
                    </div>
                  </v-card-text>
                </v-card>
              </div>

              <!-- Charter form (only when 'create' is selected) -->
              <div v-if="charterChoice === 'create'">
                <v-text-field v-model="charterForm.ownerName"
                  :label="t('charter.choice.createTitle') + ' — Operator'"
                  hint="The primary operator of this agent."
                  persistent-hint class="mb-4"
                />

                <v-expansion-panels class="mb-4" multiple>
                  <v-expansion-panel v-for="section in charterSections" :key="section.key">
                    <v-expansion-panel-title>{{ section.title }}</v-expansion-panel-title>
                    <v-expansion-panel-text>
                      <div class="text-caption text-medium-emphasis mb-2">{{ section.question }}</div>
                      <v-radio-group v-model="charterForm[section.key]" density="compact">
                        <v-radio v-for="opt in section.options" :key="opt.value"
                          :value="opt.value"
                        >
                          <template #label>
                            <div>
                              <strong>{{ opt.label }}</strong>
                              <div class="text-caption text-medium-emphasis">{{ opt.desc }}</div>
                            </div>
                          </template>
                        </v-radio>
                      </v-radio-group>
                    </v-expansion-panel-text>
                  </v-expansion-panel>
                </v-expansion-panels>

                <!-- Charter preview -->
                <div class="d-flex align-center justify-space-between mb-2">
                  <div class="text-subtitle-2">Preview</div>
                  <div>
                    <v-btn v-if="!charterEditMode" size="x-small" variant="tonal" @click="enterCharterEdit">
                      Edit raw
                    </v-btn>
                    <v-btn v-else size="x-small" variant="tonal" @click="resetCharterEdit">
                      Reset to generated
                    </v-btn>
                  </div>
                </div>

                <v-textarea v-if="charterEditMode"
                  v-model="charterEditable"
                  rows="16" auto-grow
                  class="font-monospace"
                  style="font-family: monospace; font-size: 0.85rem;"
                />
                <v-card v-else variant="outlined" rounded="lg" class="pa-3 charter-preview">
                  <pre style="white-space: pre-wrap; font-size: 0.85rem;">{{ charterPreview }}</pre>
                </v-card>
              </div>

              <div class="d-flex justify-space-between mt-4">
                <v-btn variant="text" @click="prevStep">{{ t('charter.nav.back') }}</v-btn>
                <v-btn v-if="charterChoice === 'create'" color="primary" @click="nextStep">
                  {{ t('charter.nav.generate') }}
                </v-btn>
                <v-btn v-else color="primary" @click="nextStep">
                  {{ t('charter.nav.complete') }}
                </v-btn>
              </div>
            </div>

            <!-- Stage 6 was step 5 (Memory), now step 6 -->
            <!-- ======================================================== -->
            <div v-if="step === 6">
              <h3 class="text-h6 mb-3">Memory Configuration</h3>
              <p class="text-body-2 text-medium-emphasis mb-4">
                Animus uses a multi-layer memory architecture. These defaults work well for most setups.
                You can tune everything later from the configuration panel.
              </p>

              <v-text-field v-model.number="memoryForm.intakeHours"
                label="Consolidation Interval (hours)"
                type="number" min="1" max="24"
                hint="How often the agent reviews recent activity for memory consolidation."
                persistent-hint class="mb-3"
              />

              <v-switch v-model="memoryForm.reviewDaily"
                label="Daily layer review" color="primary" class="mb-1"
                hint="Review Day-layer observations daily for promotion"
                persistent-hint
              />
              <v-switch v-model="memoryForm.reviewWeekly"
                label="Weekly perspective revision" color="primary" class="mb-1"
                hint="Revisit and revise memory perspectives weekly"
                persistent-hint
              />
              <v-switch v-model="memoryForm.embeddingEnabled"
                label="Local embeddings (recommended)" color="primary" class="mb-4"
                hint="Use local embedding model for semantic search"
                persistent-hint
              />

              <div class="d-flex justify-space-between mt-4">
                <v-btn variant="text" @click="prevStep">Back</v-btn>
                <v-btn color="primary" size="large" :loading="loading"
                  @click="completeWizard">
                  Create Agent
                </v-btn>
              </div>
            </div>

            <!-- Done was step 6, now step 7 -->
            <!-- ======================================================== -->
            <div v-if="step === 7" class="text-center py-8">
              <v-icon size="64" color="success" class="mb-4">mdi-check-circle</v-icon>
              <h3 class="text-h5 mb-2">You're all set!</h3>
              <p class="text-body-1 text-medium-emphasis mb-1">
                Agent <strong>{{ agentForm.name }}</strong>
                <span v-if="createdAgentId"> (ID: {{ createdAgentId }})</span>
                is ready.
              </p>
              <p class="text-body-2 text-medium-emphasis mb-6">
                The multi-layer memory architecture has been initialized.
                <span v-if="shouldSaveCharter()">A charter has been saved to the agent's memory files.</span>
                <span v-else>No charter was configured — you can add one later.</span>
              </p>
              <v-btn color="primary" size="large" @click="goToDashboard">
                Go to Dashboard
              </v-btn>
            </div>

          </v-card-text>
        </v-card>

      </v-col>
    </v-row>
  </v-container>
</template>

<style scoped>
.wizard-card {
  max-width: 720px;
  margin: 0 auto;
}

.charter-preview {
  background: rgba(var(--v-theme-on-surface), 0.03);
  max-height: 400px;
  overflow-y: auto;
}

.locale-scroll-container {
  max-height: 280px;
  overflow-y: auto;
  border: 1px solid rgba(var(--v-theme-on-surface), 0.12);
  border-radius: 8px;
  padding: 12px 16px;
}

.charter-option-card {
  cursor: pointer;
  transition: border-color 0.15s, background 0.15s;
}
.charter-option-card:hover {
  border-color: rgb(var(--v-theme-primary));
}
.charter-option-selected {
  border-color: rgb(var(--v-theme-primary));
  background: rgba(var(--v-theme-primary), 0.05);
}

.template-category-card {
  cursor: pointer;
  transition: border-color 0.15s, background 0.15s;
}
.template-category-card:hover {
  border-color: rgb(var(--v-theme-primary));
}

.template-option-card {
  cursor: pointer;
  transition: border-color 0.15s, background 0.15s;
}
.template-option-card:hover {
  border-color: rgb(var(--v-theme-primary));
}
</style>
