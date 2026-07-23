import { useTheme as useVuetifyTheme } from 'vuetify';
import { ref, watch } from 'vue';
import { themeList, type ThemeInfo } from '../plugins/vuetify';

const STORAGE_KEY = '***';

export function useAppTheme() {
  const vuetifyTheme = useVuetifyTheme();

  const currentTheme = vuetifyTheme.current;

  // Plain ref that mirrors vuetifyTheme.name — safe for v-model binding
  const currentKey = ref(vuetifyTheme.name.value);

  // Keep ref in sync if theme changes externally
  watch(() => vuetifyTheme.name.value, (val) => {
    currentKey.value = val;
  });

  function setTheme(key: string) {
    if (!themeList.some((t) => t.key === key)) return;
    vuetifyTheme.name.value = key;
    currentKey.value = key;
    try {
      localStorage.setItem(STORAGE_KEY, key);
    } catch {
      // localStorage might be unavailable (private mode, SSR)
    }
  }

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