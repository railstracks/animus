<script setup lang="ts">
import { useI18n } from 'vue-i18n';

const { t } = useI18n();
</script>

<template>
  <v-card rounded="xl" class="panel">
    <v-card-title>{{ t('constitution.title') }}</v-card-title>
    <v-card-text>
      {{ t('constitution.placeholder') }}
    </v-card-text>
  </v-card>
</template>

<style scoped>
.panel {
  min-height: 320px;
}
</style>
