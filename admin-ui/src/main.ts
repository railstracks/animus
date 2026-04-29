import { createApp } from 'vue';
import '@mdi/font/css/materialdesignicons.css';

import App from './App.vue';
import router from './router';
import vuetify from './plugins/vuetify';

import './styles/theme.css';

createApp(App).use(router).use(vuetify).mount('#app');
