<script setup lang="ts">
import { ref, watch } from 'vue';
import { useAppTheme } from '../composables/useAppTheme';
import { useI18n } from 'vue-i18n';

const { t } = useI18n();
const { currentKey, themes, setTheme } = useAppTheme();

const themeItems = themes.map(t => ({ title: t.label, value: t.key }));

// Local ref that Vuetify's v-select owns completely
const localSelected = ref(currentKey.value);

// Sync local when external theme changes
watch(currentKey, (val) => { localSelected.value = val; });

// Apply theme when local changes (from select click)
watch(localSelected, (val) => {
  if (typeof val === 'string' && val !== currentKey.value) setTheme(val);
});
</script>

<template>
  <div>
    <h1 class="text-h5 mb-4">{{ t('theme.title', 'Theme') }}</h1>

    <!-- Theme selector -->
    <v-card variant="tonal" class="mb-6 pa-4" max-width="500">
      <div class="text-subtitle-1 mb-3">{{ t('theme.selectLabel', 'Application Theme') }}</div>
      <v-select
        v-model="localSelected"
        :items="themeItems"
        :label="t('theme.selectLabel', 'Application Theme')"
        density="compact"
        variant="outlined"
        hide-details
      />
      <p class="text-caption text-medium-emphasis mt-2">
        {{ t('theme.hint', 'Theme preference is stored in your browser and applies to this device only.') }}
      </p>
    </v-card>