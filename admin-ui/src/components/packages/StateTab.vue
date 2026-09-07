<template>
  <div>
    <p v-if="fields.length === 0" class="text-caption text-medium-emphasis">
      {{ t('packages.noState') }}
    </p>
    <v-row v-else>
      <v-col v-for="f in fields" :key="f.key" cols="12" md="6">
        <v-text-field
          v-model="f.value"
          :label="f.key + (f.secret ? ' 🔒' : '')"
          :type="f.secret && !f.reveal ? 'password' : 'text'"
          :hint="f.secret ? t('packages.secretHint') : (f.default ? 'default: ' + f.default : '')"
          persistent-hint
          density="comfortable"
          :append-inner-icon="f.secret ? (f.reveal ? 'mdi-eye-off' : 'mdi-eye') : undefined"
          @click:append-inner="f.reveal = !f.reveal"
        />
      </v-col>
      <v-col cols="12">
        <v-btn size="small" color="primary" variant="tonal" :loading="saving" @click="save">
          {{ t('packages.saveState') }}
        </v-btn>
      </v-col>
    </v-row>
  </div>
</template>

<script setup lang="ts">
import { ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import type { PackageDetail } from './types';

const { t } = useI18n();
const props = defineProps<{ detail: PackageDetail }>();
const emit = defineEmits<{ (e: 'saved'): void; (e: 'toast', text: string, color?: string): void }>();

interface StateField {
  key: string;
  value: string;
  secret: boolean;
  default: string | null;
  reveal: boolean;
}

const fields = ref<StateField[]>([]);
const saving = ref(false);

function build() {
  const out: StateField[] = [];
  let schema: Record<string, any> = {};
  try { schema = JSON.parse(props.detail.state_schema || '{}'); } catch { /* ignore */ }
  const state = props.detail.state || {};
  for (const key of Object.keys(schema)) {
    const def = schema[key] || {};
    const isSecret = !!def.secret;
    const current = state[key];
    out.push({
      key,
      // Secrets come back masked ("***") — start empty, not with the mask
      value: isSecret ? '' : (current === undefined || current === null ? '' : String(current)),
      secret: isSecret,
      default: def.default ?? null,
      reveal: false,
    });
  }
  fields.value = out;
}

watch(() => props.detail, build, { immediate: true });

async function save() {
  saving.value = true;
  try {
    const next: Record<string, any> = {};
    for (const f of fields.value) {
      if (f.secret && f.value === '') continue; // untouched secret = keep existing
      next[f.key] = f.value;
    }
    const resp = await fetch(`/api/v1/api/packages/${props.detail.id}/state`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ state: next }),
    });
    if (!resp.ok) {
      const err = await resp.json().catch(() => ({}));
      throw new Error(err.error || 'save failed');
    }
    emit('toast', t('packages.stateSaved'));
    emit('saved');
  } catch (e: any) {
    emit('toast', e.message || t('packages.stateSaveFailed'), 'error');
  } finally {
    saving.value = false;
  }
}
</script>
