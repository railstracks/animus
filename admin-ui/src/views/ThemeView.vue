<script setup lang="ts">
import { useAppTheme } from '../composables/useAppTheme';
import { useI18n } from 'vue-i18n';

const { t } = useI18n();
const { currentKey, themes, setTheme } = useAppTheme();
</script>

<template>
  <div>
    <h1 class="text-h5 mb-4">{{ t('theme.title', 'Theme') }}</h1>

    <!-- Theme selector -->
    <v-card variant="tonal" class="mb-6 pa-4" max-width="500">
      <div class="text-subtitle-1 mb-3">{{ t('theme.selectLabel', 'Application Theme') }}</div>
      <v-select
        v-model="currentKey"
        :items="themes.map(t => ({ title: t.label, value: t.key }))"
        :label="t('theme.selectLabel', 'Application Theme')"
        density="compact"
        variant="outlined"
        hide-details
      />
      <p class="text-caption text-medium-emphasis mt-2">
        {{ t('theme.hint', 'Theme preference is stored in your browser and applies to this device only.') }}
      </p>
    </v-card>

    <!-- Sample elements -->
    <v-card variant="tonal" class="mb-4 pa-4">
      <div class="text-subtitle-1 mb-3">{{ t('theme.samples', 'Sample Elements') }}</div>

      <!-- Buttons -->
      <div class="d-flex ga-2 mb-4 flex-wrap">
        <v-btn color="primary">Primary</v-btn>
        <v-btn color="secondary">Secondary</v-btn>
        <v-btn color="accent">Accent</v-btn>
        <v-btn color="info">Info</v-btn>
        <v-btn color="success">Success</v-btn>
        <v-btn color="warning">Warning</v-btn>
        <v-btn color="error">Error</v-btn>
      </div>

      <!-- Chips -->
      <div class="d-flex ga-2 mb-4 flex-wrap">
        <v-chip color="primary" variant="tonal">Primary</v-chip>
        <v-chip color="secondary" variant="tonal">Secondary</v-chip>
        <v-chip color="accent" variant="tonal">Accent</v-chip>
        <v-chip color="success" variant="tonal">Success</v-chip>
        <v-chip color="warning" variant="tonal">Warning</v-chip>
        <v-chip color="error" variant="tonal">Error</v-chip>
      </div>

      <!-- Alerts -->
      <div class="d-flex flex-column ga-2 mb-4">
        <v-alert type="info" density="compact">Info alert — something worth noting.</v-alert>
        <v-alert type="success" density="compact">Success alert — operation completed.</v-alert>
        <v-alert type="warning" density="compact">Warning alert — proceed with caution.</v-alert>
        <v-alert type="error" density="compact">Error alert — something went wrong.</v-alert>
      </div>

      <!-- Form inputs -->
      <div class="d-flex ga-3 mb-4 flex-wrap">
        <v-text-field label="Text Field" density="compact" style="max-width: 200px;" />
        <v-select
          label="Select"
          :items="['Option A', 'Option B', 'Option C']"
          density="compact"
          style="max-width: 200px;"
        />
        <v-switch label="Toggle" density="compact" color="primary" hide-details />
      </div>

      <!-- Cards -->
      <div class="d-flex ga-3 mb-4 flex-wrap">
        <v-card variant="tonal" class="pa-3" max-width="220">
          <div class="text-subtitle-2 mb-1">Tonal Card</div>
          <p class="text-body-2">Surface variant with tonal styling.</p>
        </v-card>
        <v-card variant="outlined" class="pa-3" max-width="220">
          <div class="text-subtitle-2 mb-1">Outlined Card</div>
          <p class="text-body-2">Outlined card with no fill.</p>
        </v-card>
        <v-card variant="flat" class="pa-3" max-width="220">
          <div class="text-subtitle-2 mb-1">Flat Card</div>
          <p class="text-body-2">Flat surface with no border.</p>
        </v-card>
      </div>

      <!-- Progress -->
      <div class="d-flex ga-4 mb-4 align-center">
        <v-progress-circular color="primary" indeterminate />
        <v-progress-linear color="primary" model-value="65" style="max-width: 200px;" />
      </div>

      <!-- Text styles -->
      <div class="mb-2">
        <div class="text-h6">Heading Text</div>
        <div class="text-body-1">Body text in the current theme.</div>
        <div class="text-caption text-medium-emphasis">Caption with medium emphasis.</div>
      </div>

      <!-- Table -->
      <v-table density="compact">
        <thead>
          <tr>
            <th>Name</th>
            <th>Status</th>
            <th>Value</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td>Item A</td>
            <td><v-chip size="x-small" color="success" variant="tonal">Active</v-chip></td>
            <td>1,240</td>
          </tr>
          <tr>
            <td>Item B</td>
            <td><v-chip size="x-small" color="warning" variant="tonal">Pending</v-chip></td>
            <td>892</td>
          </tr>
          <tr>
            <td>Item C</td>
            <td><v-chip size="x-small" color="error" variant="tonal">Failed</v-chip></td>
            <td>0</td>
          </tr>
        </tbody>
      </v-table>
    </v-card>
  </div>
</template>