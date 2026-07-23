// Application theme system — CSS class based, independent of Vuetify's theme reactivity.
//
// Themes override Vuetify's CSS custom properties by targeting
// .v-application.theme-<name> for specificity over Vuetify's inline styles.

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
const ROOT_SELECTOR = '.v-application';

function getRoot(): HTMLElement | null {
  return document.querySelector(ROOT_SELECTOR);
}

export function getCurrentTheme(): string {
  try {
    return localStorage.getItem(STORAGE_KEY) || 'animusDark';
  } catch {
    return 'animusDark';
  }
}

export function applyTheme(key: string) {
  const root = getRoot();
  if (!root) return;
  for (const t of themeList) {
    root.classList.remove(`theme-${t.key}`);
  }
  root.classList.add(`theme-${key}`);
  root.setAttribute('data-theme', key);
  const info = themeList.find(t => t.key === key);
  if (info) {
    root.setAttribute('data-dark', info.dark ? 'true' : 'false');
  }
  try {
    localStorage.setItem(STORAGE_KEY, key);
  } catch {}
}

export const themeCss = `
/* ── Animus Dark (original) ── */
.v-application.theme-animusDark {
  --v-theme-background: 15, 17, 23;
  --v-theme-surface: 23, 26, 35;
  --v-theme-surface-variant: 31, 35, 48;
  --v-theme-primary: 46, 196, 182;
  --v-theme-secondary: 255, 159, 28;
  --v-theme-accent: 231, 29, 54;
  --v-theme-info: 58, 134, 255;
  --v-theme-success: 99, 212, 113;
  --v-theme-warning: 255, 191, 105;
  --v-theme-error: 255, 93, 115;
  --v-theme-on-surface: 255, 255, 255;
  --v-theme-on-background: 255, 255, 255;
  --v-theme-on-primary: 255, 255, 255;
  --v-theme-on-secondary: 255, 255, 255;
  --v-theme-on-accent: 255, 255, 255;
  --v-border-color: 255, 255, 255;
  --v-border-opacity: 0.08;
  color: rgb(255, 255, 255);
  background: rgb(15, 17, 23);
}

/* ── Animus Light ── */
.v-application.theme-animusLight {
  --v-theme-background: 244, 243, 239;
  --v-theme-surface: 255, 255, 255;
  --v-theme-surface-variant: 232, 230, 224;
  --v-theme-primary: 26, 156, 145;
  --v-theme-secondary: 232, 132, 16;
  --v-theme-accent: 196, 22, 45;
  --v-theme-info: 37, 99, 235;
  --v-theme-success: 61, 167, 86;
  --v-theme-warning: 217, 119, 6;
  --v-theme-error: 220, 38, 38;
  --v-theme-on-surface: 30, 30, 30;
  --v-theme-on-background: 30, 30, 30;
  --v-theme-on-primary: 255, 255, 255;
  --v-theme-on-secondary: 255, 255, 255;
  --v-theme-on-accent: 255, 255, 255;
  --v-border-color: 0, 0, 0;
  --v-border-opacity: 0.12;
  color: rgb(30, 30, 30);
  background: rgb(244, 243, 239);
}

/* ── Midnight ── */
.v-application.theme-midnight {
  --v-theme-background: 10, 14, 26;
  --v-theme-surface: 17, 23, 38;
  --v-theme-surface-variant: 26, 34, 56;
  --v-theme-primary: 124, 108, 255;
  --v-theme-secondary: 79, 195, 247;
  --v-theme-accent: 236, 64, 122;
  --v-theme-info: 92, 156, 230;
  --v-theme-success: 102, 187, 106;
  --v-theme-warning: 255, 183, 77;
  --v-theme-error: 239, 83, 80;
  --v-theme-on-surface: 255, 255, 255;
  --v-theme-on-background: 255, 255, 255;
  --v-theme-on-primary: 255, 255, 255;
  --v-theme-on-secondary: 255, 255, 255;
  --v-theme-on-accent: 255, 255, 255;
  --v-border-color: 255, 255, 255;
  --v-border-opacity: 0.08;
  color: rgb(255, 255, 255);
  background: rgb(10, 14, 26);
}

/* ── Ember ── */
.v-application.theme-ember {
  --v-theme-background: 26, 20, 16;
  --v-theme-surface: 34, 26, 20;
  --v-theme-surface-variant: 45, 34, 24;
  --v-theme-primary: 217, 119, 6;
  --v-theme-secondary: 180, 83, 9;
  --v-theme-accent: 220, 38, 38;
  --v-theme-info: 59, 130, 246;
  --v-theme-success: 101, 163, 13;
  --v-theme-warning: 245, 158, 11;
  --v-theme-error: 239, 68, 68;
  --v-theme-on-surface: 255, 240, 230;
  --v-theme-on-background: 255, 240, 230;
  --v-theme-on-primary: 255, 255, 255;
  --v-theme-on-secondary: 255, 255, 255;
  --v-theme-on-accent: 255, 255, 255;
  --v-border-color: 255, 240, 230;
  --v-border-opacity: 0.08;
  color: rgb(255, 240, 230);
  background: rgb(26, 20, 16);
}
`;