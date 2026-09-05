<template>
  <div>
    <p v-if="connections.length === 0" class="text-caption text-medium-emphasis">
      {{ t('packages.noConnections') }}
    </p>
    <v-table v-else density="compact">
      <thead>
        <tr>
          <th>{{ t('packages.name') }}</th>
          <th>{{ t('packages.type') }}</th>
          <th>Poll</th>
          <th>{{ t('packages.hooks') }}</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="c in connections" :key="c.id">
          <td>{{ c.name }}</td>
          <td>{{ c.type }}</td>
          <td>{{ c.poll ? c.poll + 's' : '—' }}</td>
          <td class="text-caption">{{ c.hooks }}</td>
        </tr>
      </tbody>
    </v-table>
  </div>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import type { ConnectionRow } from './types';

const { t } = useI18n();
defineProps<{ connections: ConnectionRow[] }>();
</script>
