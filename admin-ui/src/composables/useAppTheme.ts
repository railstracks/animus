import { useTheme as useVuetifyTheme } from 'vuetify';
import { ref, watch } from 'vue';
import { themeList, type ThemeInfo } from '../plugins/vuetify';

const STORAGE_KEY = '***';

export function useAppTheme() {
  const vuetifyTheme = useVuetifyTheme();

  const currentTheme = vuetifyTheme.current;

  // Plain ref for v-model binding — Vuetify's v-select owns this freely
  const currentKey = ref(vuetifyTheme.name.value);

  // Apply side effects when currentKey changes (from v-model)
  watch(currentKey, (key) => {
    if (!themeList.some((t) => t.key === key)) return;
    vuetifyTheme.name.value = key;
    try {
      localStorage.setItem(STORAGE_KEY, key);
    } catch {
      // localStorage might be unavailable
    }
  });

  // Keep ref in sync if theme changes externally
  watch(() => vuetifyTheme.name.value, (val) => {
    if (val !== currentKey.value) currentKey.value = val;
  });

  function setTheme(key: string) {
    currentKey.value = key;
  }

  function cycleTheme() {
    const idx = themeList.findIndex((t) => t.key === currentKey.value);
    const next = themeList[(idx + 1) % themeList.length];
    setTheme(next.key);
  }

  return {
    currentTheme,
    currentKey,
    themes: themeList,
    setTheme,
    cycleTheme,
  };
}