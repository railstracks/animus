import { useTheme as useVuetifyTheme } from 'vuetify';
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

  function cycleTheme() {
    const idx = themeList.findIndex((t) => t.key === vuetifyTheme.name.value);
    const next = themeList[(idx + 1) % themeList.length];
    setTheme(next.key);
  }

  return {
    currentTheme,
    currentKey: vuetifyTheme.name,
    themes: themeList,
    setTheme,
    cycleTheme,
  };
}