import { useTheme as useVuetifyTheme } from 'vuetify';
import { computed } from 'vue';
import { themeList, type ThemeInfo } from '../plugins/vuetify';

const STORAGE_KEY = 'animus-theme';

export function useAppTheme() {
  const vuetifyTheme = useVuetifyTheme();

  const currentTheme = vuetifyTheme.current;

  function setTheme(key: string) {
    if (!themeList.some((t) => t.key === key)) return;
    vuetifyTheme.name.value = key;
    try {
      localStorage.setItem(STORAGE_KEY, key);
    } catch {
      // localStorage might be unavailable (private mode, SSR)
    }
  }

  // Writable computed so v-select v-model works without readonly errors
  const currentKey = computed({
    get: () => vuetifyTheme.name.value,
    set: (val: string) => setTheme(val),
  });

  function cycleTheme() {
    const idx = themeList.findIndex((t) => t.key === vuetifyTheme.name.value);
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