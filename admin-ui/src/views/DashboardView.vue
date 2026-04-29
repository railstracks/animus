<script setup lang="ts">
import { onMounted, ref } from 'vue';

import { apiGet } from '../lib/api';

const status = ref('loading');
const uptime = ref<number | null>(null);
const error = ref('');

onMounted(async () => {
  try {
    const payload = await apiGet<{ status?: string; uptime?: number }>('/api/v1/status');
    status.value = payload.status ?? 'unknown';
    uptime.value = payload.uptime ?? null;
  } catch (err) {
    status.value = 'error';
    error.value = err instanceof Error ? err.message : 'Unknown error';
  }
});
</script>

<template>
  <div class="grid">
    <v-card class="panel" rounded="xl">
      <v-card-title>System Status</v-card-title>
      <v-card-text>
        <div class="metric">
          <span class="metric-label">State</span>
          <span class="metric-value">{{ status }}</span>
        </div>
        <div class="metric">
          <span class="metric-label">Uptime (s)</span>
          <span class="metric-value">{{ uptime ?? '—' }}</span>
        </div>
        <div v-if="error" class="error">{{ error }}</div>
      </v-card-text>
    </v-card>

    <v-card class="panel" rounded="xl">
      <v-card-title>Recent Activity</v-card-title>
      <v-card-text>
        Placeholder summary. Ticket 010/011 will populate live session and memory activity.
      </v-card-text>
    </v-card>
  </div>
</template>

<style scoped>
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 1rem;
}

.panel {
  background: linear-gradient(145deg, rgba(255, 255, 255, 0.04), rgba(255, 255, 255, 0.01));
}

.metric {
  display: flex;
  justify-content: space-between;
  margin: 0.5rem 0;
}

.metric-label {
  opacity: 0.7;
}

.metric-value {
  font-weight: 600;
}

.error {
  margin-top: 0.75rem;
  color: #ff5d73;
}
</style>
