<script setup lang="ts">
import { ref, computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute } from 'vue-router';

import AppSidebar from './components/AppSidebar.vue';
import { setLocale, isLocaleType, LocaleSelectItems, LocaleSelectItem } from './i18n';
import { useAppTheme } from './composables/useAppTheme';

const route = useRoute();
const drawer = ref(true);
const { t, locale } = useI18n();
const { currentKey, themes, setTheme, cycleTheme } = useAppTheme();

const currentThemeLabel = computed(() =>
  themes.find((t) => t.key === currentKey.value)?.label || 'Animus Dark'
);

const isWizard = computed(() => route.name === 'wizard' || route.name === 'login');

const localeItems = LocaleSelectItems.map((loc: LocaleSelectItem) => ({
  title: loc.label,
  value: loc.value
}));

function onLocaleChange(value: unknown): void {
  if (!isLocaleType(value)) {
    return;
  }
  setLocale(value);
}
</script>

<template>
  <v-app>
    <v-navigation-drawer v-if="!isWizard" v-model="drawer" class="border-e" width="280" app>
      <div class="brand">
        <div class="brand-name">{{ t('app.brand.name') }}</div>
        <div class="brand-subtitle">{{ t('app.brand.subtitle') }}</div>
      </div>
      <AppSidebar />
    </v-navigation-drawer>

    <v-app-bar v-if="!isWizard" flat class="app-bar" app>
      <v-app-bar-nav-icon @click="drawer = !drawer" />
      <v-toolbar-title>{{ t('app.toolbar.title') }}</v-toolbar-title>
      <v-spacer />
      <div class="d-flex align-center ga-2">
        <v-btn icon variant="text" size="small" @click="cycleTheme">
          <v-icon size="small">mdi-palette</v-icon>
          <v-tooltip activator="parent" location="bottom">Theme: {{ currentThemeLabel }}</v-tooltip>
        </v-btn>
        <div class="locale-select-wrap">
          <v-select
            :model-value="locale"
            :label="t('app.toolbar.languageLabel')"
            :items="localeItems"
            density="compact"
            variant="underlined"
            hide-details
            class="locale-select"
            @update:model-value="onLocaleChange"
          />
        </div>
      </div>
    </v-app-bar>

    <v-main>
      <v-container fluid class="main-container">
        <RouterView />
      </v-container>
    </v-main>
  </v-app>
</template>

<style scoped>
.brand {
  padding: 1.5rem;
  border-bottom: 1px solid rgba(var(--v-border-color), var(--v-border-opacity));
}

.brand-name {
  font-size: 1.25rem;
  letter-spacing: 0.02em;
  font-weight: 700;
}

.brand-subtitle {
  opacity: 0.72;
  font-size: 0.85rem;
  margin-top: 0.25rem;
}

.app-bar {
  border-bottom: 1px solid rgba(var(--v-border-color), var(--v-border-opacity));
}

.main-container {
  min-height: calc(100vh - 64px);
  padding: 1.25rem;
}

.locale-select-wrap {
  min-width: 140px;
  margin-inline-end: 0.75rem;
}

.locale-select {
  max-width: 160px;
}
</style>
