import { createApp } from 'vue';
import '@mdi/font/css/materialdesignicons.css';

import App from './App.vue';
import { i18n } from './i18n';
import router from './router';
import vuetify from './plugins/vuetify';

import './styles/theme.css';

// Global fetch interceptor: inject auth token + handle 401 redirects
const originalFetch = window.fetch;
window.fetch = function(input: RequestInfo | URL, init?: RequestInit): Promise<Response> {
  let url: string;
  if (typeof input === 'string') {
    url = input;
  } else if (input instanceof URL) {
    url = input.href;
  } else {
    url = input.url;
  }

  // Match both relative (/api/...) and absolute (http://host/api/...) URLs
  const isApi = url.includes('/api/');

  if (isApi) {
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

  return originalFetch.call(this, input, init).then((resp: Response) => {
    // Redirect to login on 401 from any API call (except auth endpoints)
    if (isApi && resp.status === 401 && !url.includes('/api/v1/auth/')) {
      localStorage.removeItem('animus_auth_token');
      if (!window.location.pathname.startsWith('/login')) {
        window.location.href = '/login';
      }
    }
    return resp;
  });
};

createApp(App).use(router).use(vuetify).use(i18n).mount('#app');
