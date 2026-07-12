import 'vuetify/styles';
import { createVuetify } from 'vuetify';
import * as components from 'vuetify/components';
import * as directives from 'vuetify/directives';

export default createVuetify({
  components,
  directives,
  theme: {
    defaultTheme: 'animusDark',
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
          error: '#ff5d73'
        }
      }
    }
  }
});
