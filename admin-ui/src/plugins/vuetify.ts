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
    },
  },
});