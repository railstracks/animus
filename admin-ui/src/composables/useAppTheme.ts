import { useTheme } from 'vuetify';

export interface ThemeInfo {
  key: string;
  label: string;
  dark: boolean;
}

export const themeList: ThemeInfo[] = [
  { key: 'animusDark',  label: 'Animus Dark',  dark: true  },
  { key: 'animusLight', label: 'Animus Light', dark: false },
  { key: 'midnight',    label: 'Midnight',     dark: true  },
  { key: 'ember',       label: 'Ember',        dark: true  },
];

const STORAGE_KEY = '***';

export function getCurrentTheme(): string {
  try {
    return localStorage.getItem(STORAGE_KEY) || 'animusDark';
  } catch {
    return 'animusDark';
  }
}

export function useAppTheme() {
  const theme = useTheme();

  function applyTheme(key: string) {
    if (!themeList.some((t) => t.key === key)) return;
    theme.name.value = key;
    try {
      localStorage.setItem(STORAGE_KEY, key);
    } catch {}
  }

  // Apply saved theme on mount — called from App.vue
  function initTheme() {
    applyTheme(getCurrentTheme());
  }

  return { theme, themeList, applyTheme, initTheme };
}