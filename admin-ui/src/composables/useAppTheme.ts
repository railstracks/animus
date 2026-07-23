import { useTheme as useVuetifyTheme } from 'vuetify';
import { ref, watch } from 'vue';
import { themeList, type ThemeInfo } from '../plugins/vuetify';

const STORAGE_KEY = '***';

export function useAppTheme() {
  const vuetifyTheme = useVuetifyTheme();
  const currentTheme = vuetifyTheme.current;

  // Simple read-only ref for display purposes — never passed to v-model
  const currentKey = ref(vuetifyTheme.name.value);
  watch(() => vuetifyTheme.name.value, (val) => { currentKey.value = val; });

  function setTheme(key: string) {
    if (!themeList.some((t) => t.key === key)) return;
    vuetifyTheme.name.value = key;
    try {
      localStorage.setItem(STORAGE_KEY, key);
    } catch {
      // localStorage might be unavailable
    }
  }

  function cycleTheme() {
    const idx = themeList.findIndex((t) => t.key === currentKey.value);
    const next = themeList[(idx + 1) % themeList.length];
    setTheme(next.key);
  }

  return { currentTheme, currentKey, themes: themeList, setTheme, cycleTheme };
}