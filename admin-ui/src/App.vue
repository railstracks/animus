<script setup lang="ts">
import { ref, computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRoute } from 'vue-router';

import AppSidebar from './components/AppSidebar.vue';
import { setLocale, isLocaleType, LocaleSelectItems, LocaleSelectItem } from './i18n';

const route = useRoute();
const drawer = ref(true);
const { t, locale } = useI18n();

const isWizard = computed(() => route.name === 'wizard');

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
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
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
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
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
