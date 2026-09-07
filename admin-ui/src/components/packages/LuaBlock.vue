<template>
  <div class="lua-block rounded">
    <div class="d-flex align-center justify-space-between lua-toolbar px-2">
      <span class="text-caption lua-filename">{{ filename || 'lua' }}</span>
      <div class="d-flex align-center">
        <span v-if="copied" class="text-caption lua-copied mr-2">{{ t('packages.copied') }}</span>
        <v-btn icon size="x-small" variant="text" @click="copy">
          <v-icon size="small" color="grey">mdi-content-copy</v-icon>
        </v-btn>
      </div>
    </div>
    <div class="lua-body" :style="{ maxHeight }">
      <pre class="lua-gutter">{{ gutter }}</pre>
      <pre class="lua-code" v-html="highlighted"></pre>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue';
import { useI18n } from 'vue-i18n';

const { t } = useI18n();

const props = withDefaults(defineProps<{
  code: string;
  filename?: string;
  maxHeight?: string;
}>(), {
  filename: 'script.lua',
  maxHeight: '420px',
});

const copied = ref(false);
let copiedTimer: ReturnType<typeof setTimeout> | undefined;

const gutter = computed(() => props.code.split('\n').map((_, i) => i + 1).join('\n'));

// ---------------------------------------------------------------------------
// Hand-rolled Lua highlighting (no dependency; bundle already at 500kB warn).
// Token classes: c=comment s=string n=number k=keyword g=common global.
// Everything is HTML-escaped; only our own <span> markup is ever injected.
// ---------------------------------------------------------------------------
const LUA_TOKEN = new RegExp(
  [
    '(--\\[\\[[\\s\\S]*?\\]\\]|--[^\\n]*)',                                  // 1 comments
    '(\\[\\[[\\s\\S]*?\\]\\]|"(?:[^"\\\\]|\\\\.)*"|\'(?:[^\'\\\\]|\\\\.)*\')', // 2 strings
    '\\b(0x[0-9a-fA-F]+|\\d+\\.?\\d*(?:[eE][+-]?\\d+)?)\\b',                  // 3 numbers
    '\\b(and|break|do|else|elseif|end|for|function|goto|if|in|local|not|or'
    + '|repeat|return|then|until|while|false|true|nil)\\b',                   // 4 keywords
    '\\b(ctx|state|pairs|ipairs|string|table|math|os|tostring|tonumber'
    + '|print|error|pcall|type|next|select|require)\\b',                      // 5 globals
  ].join('|'),
  'g',
);

function esc(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

const highlighted = computed(() => {
  const src = props.code || '';
  let out = '';
  let last = 0;
  src.replace(LUA_TOKEN, (m: string, comment: string, str: string, num: string, kw: string, glob: string, offset: number) => {
    out += esc(src.slice(last, offset));
    const cls = comment ? 'c' : str ? 's' : num ? 'n' : kw ? 'k' : 'g';
    out += `<span class="t-${cls}">${esc(m)}</span>`;
    last = offset + m.length;
    return m;
  });
  out += esc(src.slice(last));
  return out;
});

async function copy() {
  try {
    await navigator.clipboard.writeText(props.code);
    copied.value = true;
    if (copiedTimer) clearTimeout(copiedTimer);
    copiedTimer = setTimeout(() => { copied.value = false; }, 1500);
  } catch {
    /* clipboard unavailable (insecure context) — silent */
  }
}
</script>

<style scoped>
.lua-block {
  background: #1e1e2e;
  border: 1px solid rgba(255, 255, 255, 0.12);
  overflow: hidden;
}
.lua-toolbar {
  background: rgba(255, 255, 255, 0.05);
  min-height: 28px;
}
.lua-filename {
  color: #9399b2;
}
.lua-copied {
  color: #a6e3a1;
}
.lua-body {
  display: flex;
  overflow: auto;
}
.lua-gutter {
  text-align: right;
  color: #585b70;
  padding: 8px 6px 8px 10px;
  user-select: none;
  min-width: 2.5em;
  margin: 0;
}
.lua-code {
  flex: 1;
  padding: 8px 12px;
  margin: 0;
  color: #cdd6f4;
}
.lua-gutter,
.lua-code {
  font-family: 'JetBrains Mono', 'Fira Code', ui-monospace, monospace;
  font-size: 12px;
  line-height: 1.55;
}
.t-c { color: #6c7086; font-style: italic; }
.t-s { color: #a6e3a1; }
.t-n { color: #fab387; }
.t-k { color: #cba6f7; }
.t-g { color: #89b4fa; }
</style>
