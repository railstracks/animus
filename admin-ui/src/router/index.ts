import { createRouter, createWebHistory } from 'vue-router';

import ChatView from '../views/ChatView.vue';
import ConfigView from '../views/ConfigView.vue';
import ConstitutionView from '../views/ConstitutionView.vue';
import DashboardView from '../views/DashboardView.vue';
import InterfacesView from '../views/InterfacesView.vue';
import LogsView from '../views/LogsView.vue';
import MemoryView from '../views/MemoryView.vue';

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', name: 'dashboard', component: DashboardView },
    { path: '/chat', name: 'chat', component: ChatView },
    { path: '/memory', name: 'memory', component: MemoryView },
    { path: '/config', name: 'config', component: ConfigView },
    { path: '/interfaces', name: 'interfaces', component: InterfacesView },
    { path: '/constitution', name: 'constitution', component: ConstitutionView },
    { path: '/logs', name: 'logs', component: LogsView }
  ]
});

export default router;
