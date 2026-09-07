<template>
  <div>
    <p v-if="actionCommands.length === 0" class="text-caption text-medium-emphasis">
      {{ t('packages.noActions') }}
    </p>
    <v-row v-else>
      <v-col cols="12" md="4">
        <v-select
          v-model="command"
          :items="actionCommands"
          item-title="name"
          item-value="name"
          :label="t('packages.command')"
          density="comfortable"
          hide-details
        />
      </v-col>
      <v-col cols="12" md="8">
        <v-textarea
          v-model="args"
          :label="t('packages.argsLabel')"
          rows="2"
          auto-grow
          density="comfortable"
          hide-details
          placeholder='{ "symbol": "AAPL" }'
        />
      </v-col>
      <v-col cols="12">
        <v-btn size="small" color="primary" :loading="executing" :disabled="!command" @click="run">
          <v-icon start size="small">mdi-play</v-icon>
          {{ t('packages.execute') }}
        </v-btn>
      </v-col>
    </v-row>
    <pre
      v-if="result !== null"
      class="mt-2 pa-3 rounded"
      :class="result && result.success === false ? 'exec-error' : 'exec-ok'"
      style="max-height: 260px; overflow: auto;"
    >{{ JSON.stringify(result, null, 2) }}</pre>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import type { PackageDetail } from './types';

const { t } = useI18n();
const props = defineProps<{ detail: PackageDetail }>();
const emit = defineEmits<{ (e: 'toast', text: string, color?: string): void }>();

const command = ref<string | null>(null);
const args = ref('');
const executing = ref(false);
const result = ref<any>(null);

const actionCommands = computed(() =>
  (props.detail.commands || []).filter(c => c.kind === 'action'),
);

watch(() => props.detail.id, () => {
  command.value = null;
  args.value = '';
  result.value = null;
});

async function run() {
  if (!command.value) return;
  let parsed: any = {};
  if (args.value.trim()) {
    try {
      parsed = JSON.parse(args.value);
    } catch {
      emit('toast', t('packages.invalidJson'), 'error');
      return;
    }
  }
  executing.value = true;
  result.value = null;
  try {
    const resp = await fetch(`/api/v1/api/packages/${props.detail.id}/execute`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ command: command.value, args: parsed }),
    });
    result.value = await resp.json().catch(() => ({ error: 'no response body', success: false }));
  } catch (e: any) {
    result.value = { error: e.message, success: false };
  } finally {
    executing.value = false;
  }
}
</script>

<style scoped>
.exec-ok {
  background: rgba(76, 175, 80, 0.08);
}
.exec-error {
  background: rgba(244, 67, 54, 0.08);
}
</style>
