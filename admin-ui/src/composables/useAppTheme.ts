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

export function applyTheme(key: string) {
  if (!themeList.some((t) => t.key === key)) return;
  try {
    localStorage.setItem(STORAGE_KEY, key);
  } catch {}
}

// Must be called from a component setup context
export function useAppTheme() {
  const theme = useTheme();

  function setTheme(key: string) {
    if (!themeList.some((t) => t.key === key)) return;
    // Use $nextTick to avoid writing during reactive propagation
    theme.name.value = key;
    applyTheme(key);
  }

  function initTheme() {
    const saved = getCurrentTheme();
    theme.name.value = saved;
  }

  return { theme, themeList, setTheme, initTheme };
}