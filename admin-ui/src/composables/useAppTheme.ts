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
  { key: 'forest',      label: 'Forest',       dark: true  },
  { key: 'ocean',       label: 'Ocean',        dark: true  },
  { key: 'rose',        label: 'Rose',         dark: true  },
  { key: 'sand',        label: 'Sand',         dark: false },
  { key: 'mono',        label: 'Mono',         dark: true  },
  { key: 'solarized',   label: 'Solarized',    dark: true  },
  { key: 'copper',      label: 'Copper',       dark: true  },
  { key: 'nord',        label: 'Nord',         dark: true  },
  { key: 'dracula',     label: 'Dracula',      dark: true  },
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

  function setTheme(key: string) {
    if (!themeList.some((t) => t.key === key)) return;
    // Use Vuetify's change() API — theme.name is readonly in component context
    theme.change(key);
    try {
      localStorage.setItem(STORAGE_KEY, key);
    } catch {}
  }

  function initTheme() {
    setTheme(getCurrentTheme());
  }

  return { theme, themeList, setTheme, initTheme };
}