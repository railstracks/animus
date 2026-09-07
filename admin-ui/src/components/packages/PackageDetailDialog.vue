<template>
  <v-dialog :model-value="modelValue" max-width="1000" scrollable @update:model-value="v => emit('update:modelValue', v)">
    <v-card v-if="detail" rounded="lg">
      <v-card-item>
        <v-card-title class="text-subtitle-1 d-flex align-center flex-wrap">
          {{ detail.display_name || detail.name }}
          <v-chip size="x-small" variant="outlined" class="ml-3">v{{ detail.version }}</v-chip>
          <v-chip v-if="detail.registry_source" size="x-small" color="primary" variant="tonal" class="ml-2">
            <v-icon start size="x-small">mdi-cloud-download-outline</v-icon>
            {{ detail.registry_source }}
          </v-chip>
          <v-chip v-if="detail.locally_modified" size="x-small" color="warning" variant="tonal" class="ml-2">
            {{ t('packages.modified') }}
          </v-chip>
          <v-spacer />
          <v-switch
            :model-value="detail.enabled"
            color="success"
            density="compact"
            hide-details
            :label="detail.enabled ? t('packages.enabled') : t('packages.disabled')"
            :loading="toggling"
            @update:model-value="toggleEnabled"
          />
        </v-card-title>
        <v-card-subtitle>{{ detail.description }}</v-card-subtitle>
      </v-card-item>

      <v-divider />

      <v-card-text class="pt-0">
        <v-tabs v-model="tab" density="compact" color="primary">
          <v-tab value="state">{{ t('packages.tabState') }}</v-tab>
          <v-tab value="actions">{{ t('packages.tabActions') }} ({{ actionCommands.length }})</v-tab>
          <v-tab value="hooks">{{ t('packages.tabHooks') }} ({{ hookCommands.length }})</v-tab>
          <v-tab value="connections">{{ t('packages.tabConnections') }} ({{ detail.connections?.length || 0 }})</v-tab>
          <v-tab value="console">{{ t('packages.tabConsole') }}</v-tab>
        </v-tabs>

        <div class="tab-body">
          <v-tabs-window v-model="tab">
            <v-tabs-window-item value="state">
              <StateTab :detail="detail" @saved="fetchDetail" @toast="forwardToast" />
            </v-tabs-window-item>
            <v-tabs-window-item value="actions">
              <CommandsTab :commands="actionCommands" mode="actions" />
            </v-tabs-window-item>
            <v-tabs-window-item value="hooks">
              <CommandsTab
                :commands="hookCommands"
                mode="hooks"
                :state="detail.state"
                :state-schema="detail.state_schema"
              />
            </v-tabs-window-item>
            <v-tabs-window-item value="connections">
              <ConnectionsTab :connections="detail.connections || []" />
            </v-tabs-window-item>
            <v-tabs-window-item value="console">
              <ConsoleTab :detail="detail" @toast="forwardToast" />
            </v-tabs-window-item>
          </v-tabs-window>
        </div>
      </v-card-text>

      <v-divider />
      <v-card-actions>
        <v-btn color="error" variant="text" :loading="uninstalling" @click="uninstall">
          <v-icon start>mdi-delete-outline</v-icon>
          {{ t('packages.uninstall') }}
        </v-btn>
        <v-spacer />
        <v-btn variant="text" @click="emit('update:modelValue', false)">{{ t('packages.close') }}</v-btn>
      </v-card-actions>
    </v-card>

    <v-snackbar v-model="snackbar.show" :color="snackbar.color" timeout="4000">
      {{ snackbar.text }}
    </v-snackbar>
  </v-dialog>
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import StateTab from './StateTab.vue';
import CommandsTab from './CommandsTab.vue';
import ConnectionsTab from './ConnectionsTab.vue';
import ConsoleTab from './ConsoleTab.vue';
import type { PackageDetail } from './types';

const { t } = useI18n();

const props = defineProps<{
  modelValue: boolean;
  pkgId: string | null;
}>();
const emit = defineEmits<{
  (e: 'update:modelValue', v: boolean): void;
  (e: 'changed'): void;
}>();

const detail = ref<PackageDetail | null>(null);
const tab = ref('state');
const toggling = ref(false);
const uninstalling = ref(false);
const snackbar = ref({ show: false, text: '', color: 'success' });

const actionCommands = computed(() => (detail.value?.commands || []).filter(c => c.kind === 'action'));
const hookCommands = computed(() => (detail.value?.commands || []).filter(c => c.kind === 'hook'));

function forwardToast(text: string, color = 'success') {
  snackbar.value = { show: true, text, color };
}

async function fetchDetail() {
  if (!props.pkgId) return;
  try {
    const resp = await fetch(`/api/v1/api/packages/${props.pkgId}`);
    if (!resp.ok) throw new Error('detail failed');
    detail.value = await resp.json();
  } catch (e) {
    console.error(e);
    forwardToast(t('packages.loadFailed'), 'error');
  }
}

watch(() => [props.modelValue, props.pkgId] as const, ([open, id]) => {
  if (open && id) {
    tab.value = 'state';
    fetchDetail();
  }
  if (!open) detail.value = null;
});

async function toggleEnabled(enabled: boolean) {
  if (!detail.value) return;
  toggling.value = true;
  try {
    const resp = await fetch(`/api/v1/api/packages/${detail.value.id}/enabled`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled }),
    });
    if (!resp.ok) throw new Error('toggle failed');
    detail.value.enabled = enabled;
    emit('changed');
  } catch (e) {
    console.error(e);
    forwardToast(t('packages.toggleFailed'), 'error');
  } finally {
    toggling.value = false;
  }
}

async function uninstall() {
  if (!detail.value) return;
  uninstalling.value = true;
  try {
    const resp = await fetch(`/api/v1/api/packages/${detail.value.id}`, { method: 'DELETE' });
    if (!resp.ok) throw new Error('uninstall failed');
    emit('update:modelValue', false);
    emit('changed');
  } catch (e) {
    console.error(e);
    forwardToast(t('packages.uninstallFailed'), 'error');
  } finally {
    uninstalling.value = false;
  }
}
</script>

<style scoped>
.tab-body {
  max-height: 58vh;
  overflow: auto;
  padding-top: 8px;
}
</style>
