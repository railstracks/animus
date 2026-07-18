import { createApp } from 'vue';
import '@mdi/font/css/materialdesignicons.css';

import App from './App.vue';
import { i18n } from './i18n';
import router from './router';
import vuetify from './plugins/vuetify';

import './styles/theme.css';

// Global fetch interceptor: inject auth token on all /api/ requests
const originalFetch = window.fetch;
window.fetch = function(input: RequestInfo | URL, init?: RequestInit): Promise<Response> {
  const url = typeof input === 'string' ? input : (input instanceof URL ? input.href : input.url);
  if (url.startsWith('/api/')) {
    const token = localStorage.getItem('animus_auth_token');
    if (token) {
      init = init || {};
      const headers = new Headers(init.headers);
      if (!headers.has('Authorization')) {
        headers.set('Authorization', `Bearer ${token}`);
      }
      init.headers = headers;
    }
  }
  return originalFetch.call(this, input, init);
};

createApp(App).use(router).use(vuetify).use(i18n).mount('#app');
