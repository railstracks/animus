import 'vuetify/styles';
import { createVuetify } from 'vuetify';
import * as components from 'vuetify/components';
import * as directives from 'vuetify/directives';

// ─────────────────────────────────────────────────────────────────────────────
// Theme definitions
//
// Each theme maps Vuetify's color tokens. 'dark: true' makes Vuetify emit
// dark-mode CSS variables (lighter text on darker backgrounds).
// ─────────────────────────────────────────────────────────────────────────────

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

export const themeDefinitions: Record<string, { dark: boolean; colors: Record<string, string> }> = {
  // The original Animus theme — teal/amber on deep navy
  animusDark: {
    dark: true,
    colors: {
      background: '#0f1117',
      surface: '#171a23',
      'surface-variant': '#1f2330',
      primary: '#2ec4b6',
      secondary: '#ff9f1c',
      accent: '#e71d36',
      info: '#3a86ff',
      success: '#63d471',
      warning: '#ffbf69',
      error: '#ff5d73',
    },
  },

  // Light counterpart — same accents on warm white
  animusLight: {
    dark: false,
    colors: {
      background: '#f4f3ef',
      surface: '#ffffff',
      'surface-variant': '#e8e6e0',
      primary: '#1a9c91',
      secondary: '#e88410',
      accent: '#c4162d',
      info: '#2563eb',
      success: '#3da756',
      warning: '#d97706',
      error: '#dc2626',
    },
  },

  // Deep blue-purple — cool and focused
  midnight: {
    dark: true,
    colors: {
      background: '#0a0e1a',
      surface: '#111726',
      'surface-variant': '#1a2238',
      primary: '#7c6cff',
      secondary: '#4fc3f7',
      accent: '#ec407a',
      info: '#5c9ce6',
      success: '#66bb6a',
      warning: '#ffb74d',
      error: '#ef5350',
    },
  },

  // Warm dark — amber/copper tones, easy on the eyes in low light
  ember: {
    dark: true,
    colors: {
      background: '#1a1410',
      surface: '#221a14',
      'surface-variant': '#2d2218',
      primary: '#d97706',
      secondary: '#b45309',
      accent: '#dc2626',
      info: '#3b82f6',
      success: '#65a30d',
      warning: '#f59e0b',
      error: '#ef4444',
    },
  },
};

// Build the themes object Vuetify expects
const vuetifyThemes: Record<string, { dark: boolean; colors: Record<string, string> }> = {};
for (const [key, def] of Object.entries(themeDefinitions)) {
  vuetifyThemes[key] = def;
}

const storedTheme = typeof localStorage !== 'undefined'
  ? localStorage.getItem('animus-theme') || 'animusDark'
  : 'animusDark';

export default createVuetify({
  components,
  directives,
  theme: {
    defaultTheme: storedTheme,
    themes: vuetifyThemes,
  },
});