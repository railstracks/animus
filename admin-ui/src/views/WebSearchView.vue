<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { useI18n } from 'vue-i18n';
import { apiGet, apiRequest } from '../lib/api';

const { t } = useI18n();

interface SearchConfig {
  provider: string;
  api_key: string;
  api_key_set: boolean;
  endpoint: string;
  default_count: number;
  rate_limit_per_minute: number;
}

interface TestResult {
  status: string;
  query: string;
  count: number;
  results: Array<{ title: string; url: string; snippet: string }>;
  error?: string;
}

const config = ref<SearchConfig>({
  provider: 'brave',
  api_key: '',
  api_key_set: false,
  endpoint: '',
  default_count: 5,
  rate_limit_per_minute: 10,
});

const saving = ref(false);
const savedMessage = ref('');
const warningMessage = ref('');

const testQuery = ref('what is the latest tech news');
const testing = ref(false);
const testResult = ref<TestResult | null>(null);
const testError = ref('');

async function loadConfig() {
  try {
    const data = await apiGet<SearchConfig>('/api/v1/search/config');
    config.value = data;
  } catch (e: any) {
    console.error('Failed to load search config:', e);
  }
}

async function saveConfig() {
  saving.value = true;
  savedMessage.value = '';
  warningMessage.value = '';
  try {
    const body: Record<string, unknown> = {
      provider: config.value.provider,
      endpoint: config.value.endpoint,
      default_count: config.value.default_count,
      rate_limit_per_minute: config.value.rate_limit_per_minute,
    };
    // Only send api_key if user typed a new one (not the masked version)
    const enteredKey = config.value.api_key;
    if (enteredKey && !enteredKey.includes('...')) {
      body.api_key = enteredKey;
    }
    const resp = await apiRequest<{ status: string; warning?: string }>(
      'PUT', '/api/v1/search/config', body
    );
    if (resp.status === 'ok') {
      savedMessage.value = t('webSearch.savedSuccess');
      if (resp.warning) {
        warningMessage.value = resp.warning;
      }
    }
    // Reload to show masked key
    await loadConfig();
  } catch (e: any) {
    savedMessage.value = t('webSearch.saveError') + ': ' + (e.message || e);
  } finally {
    saving.value = false;
  }
}

async function runTest() {
  testing.value = true;
  testResult.value = null;
  testError.value = '';
  try {
    const result = await apiRequest<TestResult>(
      'POST', '/api/v1/search/test', { query: testQuery.value }
    );
    testResult.value = result;
  } catch (e: any) {
    testError.value = e.message || String(e);
  } finally {
    testing.value = false;
  }
}

onMounted(() => {
  loadConfig();
});
</script>

<template>
  <div class="web-search-view pa-4">
    <h1 class="text-h4 mb-4">{{ t('webSearch.title') }}</h1>

    <!-- Configuration -->
    <v-card class="mb-4" max-width="800">
      <v-card-title>{{ t('webSearch.configTitle') }}</v-card-title>
      <v-card-text>
        <v-select
          v-model="config.provider"
          :label="t('webSearch.provider')"
          :items="[{ title: 'Brave Search', value: 'brave' }]"
          item-title="title"
          item-value="value"
          variant="outlined"
          density="comfortable"
          class="mb-3"
        />

        <v-text-field
          v-model="config.api_key"
          :label="t('webSearch.apiKey')"
          :placeholder="config.api_key_set ? t('webSearch.apiKeySet') : t('webSearch.apiKeyPlaceholder')"
          variant="outlined"
          density="comfortable"
          class="mb-3"
          :type="config.api_key.includes('...') ? 'text' : 'password'"
        />

        <v-text-field
          v-model.number="config.default_count"
          :label="t('webSearch.defaultCount')"
          type="number"
          variant="outlined"
          density="comfortable"
          class="mb-3"
          min="1"
          max="20"
        />

        <v-text-field
          v-model.number="config.rate_limit_per_minute"
          :label="t('webSearch.rateLimit')"
          type="number"
          variant="outlined"
          density="comfortable"
          class="mb-3"
          min="1"
        />

        <v-text-field
          v-model="config.endpoint"
          :label="t('webSearch.endpoint')"
          variant="outlined"
          density="comfortable"
          class="mb-3"
          :hint="t('webSearch.endpointHint')"
          persistent-hint
        />
      </v-card-text>
      <v-card-actions>
        <v-btn color="primary" @click="saveConfig" :loading="saving">
          {{ t('webSearch.save') }}
        </v-btn>
        <v-alert v-if="savedMessage" type="success" variant="tonal" class="ml-4" density="compact">
          {{ savedMessage }}
        </v-alert>
      </v-card-actions>
    </v-card>

    <v-alert v-if="warningMessage" type="warning" variant="tonal" class="mb-4" max-width="800">
      {{ warningMessage }}
    </v-alert>

    <!-- Test Search -->
    <v-card max-width="800">
      <v-card-title>{{ t('webSearch.testTitle') }}</v-card-title>
      <v-card-text>
        <v-text-field
          v-model="testQuery"
          :label="t('webSearch.testQueryLabel')"
          variant="outlined"
          density="comfortable"
          class="mb-3"
          @keyup.enter="runTest"
        />
        <v-btn color="secondary" @click="runTest" :loading="testing" :disabled="!config.api_key_set && !config.api_key">
          {{ t('webSearch.runTest') }}
        </v-btn>

        <v-alert v-if="testError" type="error" variant="tonal" class="mt-4" density="compact">
          {{ testError }}
        </v-alert>

        <div v-if="testResult" class="mt-4">
          <div class="text-body-2 mb-2">
            {{ testResult.count }} {{ t('webSearch.results') }}
          </div>
          <v-list v-if="testResult.results?.length" lines="three">
            <v-list-item v-for="(item, i) in testResult.results" :key="i">
              <v-list-item-title>
                <a :href="item.url" target="_blank" rel="noopener" class="text-decoration-none">
                  {{ item.title || item.url }}
                </a>
              </v-list-item-title>
              <v-list-item-subtitle>{{ item.url }}</v-list-item-subtitle>
              <v-list-item-content class="text-body-2 mt-1">
                {{ item.snippet }}
              </v-list-item-content>
            </v-list-item>
          </v-list>
        </div>
      </v-card-text>
    </v-card>
  </div>
</template>
