import { createApp } from 'vue';
import { createApp } from 'vue';
import '@mdi/font/css/materialdesignicons.css';

import App from './App.vue';
import { i18n } from './i18n';
import router from './router';
import vuetify from './plugins/vuetify';

import './styles/theme.css';

createApp(App).use(router).use(vuetify).use(i18n).mount('#app');
