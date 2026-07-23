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
  --v-theme-background: 15, 17, 23 !important;
  --v-theme-surface: 23, 26, 35 !important;
  --v-theme-surface-variant: 31, 35, 48 !important;
  --v-theme-primary: 46, 196, 182 !important;
  --v-theme-secondary: 255, 159, 28 !important;
  --v-theme-accent: 231, 29, 54 !important;
  --v-theme-info: 58, 134, 255 !important;
  --v-theme-success: 99, 212, 113 !important;
  --v-theme-warning: 255, 191, 105 !important;
  --v-theme-error: 255, 93, 115 !important;
  --v-theme-on-surface: 255, 255, 255 !important;
  --v-theme-on-background: 255, 255, 255 !important;
  --v-theme-on-primary: 255, 255, 255 !important;
  --v-theme-on-secondary: 255, 255, 255 !important;
  --v-theme-on-accent: 255, 255, 255 !important;
  --v-border-color: 255, 255, 255 !important;
  --v-border-opacity: 0.08 !important;
  color: rgb(255, 255, 255) !important;
  background: rgb(15, 17, 23) !important;
}

/* ── Animus Light ── */
.v-application.theme-animusLight {
  --v-theme-background: 244, 243, 239 !important;
  --v-theme-surface: 255, 255, 255 !important;
  --v-theme-surface-variant: 232, 230, 224 !important;
  --v-theme-primary: 26, 156, 145 !important;
  --v-theme-secondary: 232, 132, 16 !important;
  --v-theme-accent: 196, 22, 45 !important;
  --v-theme-info: 37, 99, 235 !important;
  --v-theme-success: 61, 167, 86 !important;
  --v-theme-warning: 217, 119, 6 !important;
  --v-theme-error: 220, 38, 38 !important;
  --v-theme-on-surface: 30, 30, 30 !important;
  --v-theme-on-background: 30, 30, 30 !important;
  --v-theme-on-primary: 255, 255, 255 !important;
  --v-theme-on-secondary: 255, 255, 255 !important;
  --v-theme-on-accent: 255, 255, 255 !important;
  --v-border-color: 0, 0, 0 !important;
  --v-border-opacity: 0.12 !important;
  color: rgb(30, 30, 30) !important;
  background: rgb(244, 243, 239) !important;
}

/* ── Midnight ── */
.v-application.theme-midnight {
  --v-theme-background: 10, 14, 26 !important;
  --v-theme-surface: 17, 23, 38 !important;
  --v-theme-surface-variant: 26, 34, 56 !important;
  --v-theme-primary: 124, 108, 255 !important;
  --v-theme-secondary: 79, 195, 247 !important;
  --v-theme-accent: 236, 64, 122 !important;
  --v-theme-info: 92, 156, 230 !important;
  --v-theme-success: 102, 187, 106 !important;
  --v-theme-warning: 255, 183, 77 !important;
  --v-theme-error: 239, 83, 80 !important;
  --v-theme-on-surface: 255, 255, 255 !important;
  --v-theme-on-background: 255, 255, 255 !important;
  --v-theme-on-primary: 255, 255, 255 !important;
  --v-theme-on-secondary: 255, 255, 255 !important;
  --v-theme-on-accent: 255, 255, 255 !important;
  --v-border-color: 255, 255, 255 !important;
  --v-border-opacity: 0.08 !important;
  color: rgb(255, 255, 255) !important;
  background: rgb(10, 14, 26) !important;
}

/* ── Ember ── */
.v-application.theme-ember {
  --v-theme-background: 26, 20, 16 !important;
  --v-theme-surface: 34, 26, 20 !important;
  --v-theme-surface-variant: 45, 34, 24 !important;
  --v-theme-primary: 217, 119, 6 !important;
  --v-theme-secondary: 180, 83, 9 !important;
  --v-theme-accent: 220, 38, 38 !important;
  --v-theme-info: 59, 130, 246 !important;
  --v-theme-success: 101, 163, 13 !important;
  --v-theme-warning: 245, 158, 11 !important;
  --v-theme-error: 239, 68, 68 !important;
  --v-theme-on-surface: 255, 240, 230 !important;
  --v-theme-on-background: 255, 240, 230 !important;
  --v-theme-on-primary: 255, 255, 255 !important;
  --v-theme-on-secondary: 255, 255, 255 !important;
  --v-theme-on-accent: 255, 255, 255 !important;
  --v-border-color: 255, 240, 230 !important;
  --v-border-opacity: 0.08 !important;
  color: rgb(255, 240, 230) !important;
  background: rgb(26, 20, 16) !important;
}
`;