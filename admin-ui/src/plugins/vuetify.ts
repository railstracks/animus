import 'vuetify/styles';
import { createVuetify } from 'vuetify';
import * as components from 'vuetify/components';
import * as directives from 'vuetify/directives';

const storedTheme = typeof localStorage !== 'undefined'
  ? localStorage.getItem('***') || 'animusDark'
  : 'animusDark';

export default createVuetify({
  components,
  directives,
  theme: {
    defaultTheme: storedTheme,
    themes: {
      // --- Original themes ---
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

      // --- New themes (v0.3.0) ---

      // Forest — deep green, earthy, calm
      forest: {
        dark: true,
        colors: {
          background: '#0d1410',
          surface: '#141d17',
          'surface-variant': '#1c2820',
          primary: '#4ade80',
          secondary: '#a3e635',
          accent: '#facc15',
          info: '#60a5fa',
          success: '#22c55e',
          warning: '#fbbf24',
          error: '#f87171',
        },
      },

      // Ocean — deep teal-blue, cool and professional
      ocean: {
        dark: true,
        colors: {
          background: '#0a1929',
          surface: '#0f2438',
          'surface-variant': '#16304e',
          primary: '#06b6d4',
          secondary: '#3b82f6',
          accent: '#8b5cf6',
          info: '#0ea5e9',
          success: '#10b981',
          warning: '#f59e0b',
          error: '#ef4444',
        },
      },

      // Rose — warm pink/magenta, expressive
      rose: {
        dark: true,
        colors: {
          background: '#1a0d14',
          surface: '#241420',
          'surface-variant': '#301a2a',
          primary: '#f43f5e',
          secondary: '#ec4899',
          accent: '#a855f7',
          info: '#3b82f6',
          success: '#10b981',
          warning: '#f59e0b',
          error: '#ef4444',
        },
      },

      // Sand — warm light theme, desert/parchment
      sand: {
        dark: false,
        colors: {
          background: '#faf6ef',
          surface: '#fffdf8',
          'surface-variant': '#ede4d3',
          primary: '#c2410c',
          secondary: '#d97706',
          accent: '#b91c1c',
          info: '#0369a1',
          success: '#15803d',
          warning: '#ca8a04',
          error: '#dc2626',
        },
      },

      // Mono — high-contrast grayscale, minimal
      mono: {
        dark: true,
        colors: {
          background: '#0a0a0a',
          surface: '#161616',
          'surface-variant': '#222222',
          primary: '#e5e5e5',
          secondary: '#a3a3a3',
          accent: '#737373',
          info: '#525252',
          success: '#404040',
          warning: '#737373',
          error: '#a3a3a3',
        },
      },

      // Solarized — the classic palette
      solarized: {
        dark: true,
        colors: {
          background: '#002b36',
          surface: '#073642',
          'surface-variant': '#0d4a5a',
          primary: '#268bd2',
          secondary: '#2aa198',
          accent: '#d33682',
          info: '#6c71c4',
          success: '#859900',
          warning: '#b58900',
          error: '#dc322f',
        },
      },

      // Copper — Kestrel's palette, warm metallic
      copper: {
        dark: true,
        colors: {
          background: '#1c1308',
          surface: '#261a0e',
          'surface-variant': '#332514',
          primary: '#b87333',
          secondary: '#cd7f32',
          accent: '#e8a87c',
          info: '#5b8db8',
          success: '#7a9b3a',
          warning: '#d4a02e',
          error: '#c0392b',
        },
      },

      // Nord — the popular arctic palette
      nord: {
        dark: true,
        colors: {
          background: '#2e3440',
          surface: '#3b4252',
          'surface-variant': '#434c5e',
          primary: '#88c0d0',
          secondary: '#81a1c1',
          accent: '#bf616a',
          info: '#5e81ac',
          success: '#a3be8c',
          warning: '#ebcb8b',
          error: '#bf616a',
        },
      },

      // Dracula — the cult classic
      dracula: {
        dark: true,
        colors: {
          background: '#282a36',
          surface: '#343746',
          'surface-variant': '#44475a',
          primary: '#bd93f9',
          secondary: '#8be9fd',
          accent: '#ff79c6',
          info: '#6272a4',
          success: '#50fa7b',
          warning: '#f1fa8c',
          error: '#ff5555',
        },
      },
    },
  },
});
