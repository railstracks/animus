<template>
  <div>
    <!-- ACTIONS: accordion, script in panel body -->
    <template v-if="mode === 'actions'">
      <p v-if="commands.length === 0" class="text-caption text-medium-emphasis">
        {{ t('packages.noActions') }}
      </p>
      <v-expansion-panels v-else variant="accordion">
        <v-expansion-panel v-for="c in commands" :key="c.id">
          <v-expansion-panel-title>
            <v-chip size="x-small" color="primary" variant="tonal" class="mr-2">{{ c.kind }}</v-chip>
            {{ c.name }}
          </v-expansion-panel-title>
          <v-expansion-panel-text>
            <p class="text-body-2 mb-2">{{ c.description }}</p>
            <pre v-if="c.parameters" class="text-caption mb-2">{{ c.parameters }}</pre>
            <LuaBlock v-if="c.script" :code="c.script" :filename="c.name + '.lua'" max-height="260px" />
          </v-expansion-panel-text>
        </v-expansion-panel>
      </v-expansion-panels>
    </template>

    <!-- HOOKS: grouped by event, full-width script review -->
    <template v-else>
      <p v-if="commands.length === 0" class="text-caption text-medium-emphasis">
        {{ t('packages.noHooks') }}
      </p>
      <div v-for="group in groups" :key="group.event" class="mb-4">
        <div class="d-flex align-center mb-2">
          <v-chip size="small" color="deep-purple" variant="tonal" prepend-icon="mdi-flash-outline">
            on {{ group.event }}
          </v-chip>
          <span class="text-caption text-medium-emphasis ml-2">
            {{ group.items.length === 1 ? '1 hook' : group.items.length + ' hooks' }}
          </span>
        </div>
        <div v-for="c in group.items" :key="c.id" class="mb-4">
          <div class="d-flex align-center mb-1">
            <span class="text-body-2 font-weight-medium">{{ c.name }}</span>
            <span v-if="c.description" class="text-caption text-medium-emphasis ml-3">{{ c.description }}</span>
          </div>
          <pre v-if="c.parameters" class="text-caption mb-2">{{ c.parameters }}</pre>
          <LuaBlock :code="c.script || ''" :filename="c.name + '.lua'" />
          <div class="mt-1">
            <v-btn
              v-if="stateKeys.length > 0"
              size="x-small"
              variant="text"
              @click="shownState = shownState === c.id ? null : c.id"
            >
              <v-icon start size="x-small">{{ shownState === c.id ? 'mdi-chevron-up' : 'mdi-chevron-down' }}</v-icon>
              {{ t('packages.effectiveState') }}
            </v-btn>
            <v-expand-transition>
              <div v-if="shownState === c.id" class="pa-2 rounded text-caption effective-state">
                <div v-for="k in stateKeys" :key="k.key" class="mb-1">
                  <span class="font-weight-medium">state.{{ k.key }}</span>
                  <span class="text-medium-emphasis"> = </span>
                  <span>{{ k.secret ? '•••' : k.value }}</span>
                </div>
                <div class="text-medium-emphasis mt-1">ctx.package.* — sandbox package API</div>
              </div>
            </v-expand-transition>
          </div>
        </div>
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import LuaBlock from './LuaBlock.vue';
import type { CommandRow } from './types';

const { t } = useI18n();

const props = defineProps<{
  commands: CommandRow[];
  mode: 'actions' | 'hooks';
  state?: Record<string, any>;
  stateSchema?: string;
}>();

const shownState = ref<string | null>(null);

const groups = computed(() => {
  const byEvent = new Map<string, CommandRow[]>();
  for (const c of props.commands) {
    const ev = c.event || 'unbound';
    if (!byEvent.has(ev)) byEvent.set(ev, []);
    byEvent.get(ev)!.push(c);
  }
  return [...byEvent.entries()].map(([event, items]) => ({ event, items }));
});

// Effective (non-secret) state the sandbox injects — review aid.
const stateKeys = computed(() => {
  let schema: Record<string, any> = {};
  try { schema = JSON.parse(props.stateSchema || '{}'); } catch { /* ignore */ }
  const state = props.state || {};
  return Object.keys(schema).map((key) => {
    const secret = !!(schema[key] || {}).secret;
    const raw = state[key];
    return { key, secret, value: secret ? '•••' : String(raw ?? '') };
  });
});
</script>

<style scoped>
.effective-state {
  background: rgba(128, 128, 148, 0.08);
  border: 1px dashed rgba(128, 128, 148, 0.3);
}
</style>
