import { createRouter, createWebHistory } from 'vue-router';

import AgentsView from '../views/AgentsView.vue';
import ChatView from '../views/ChatView.vue';
import WizardView from '../views/WizardView.vue';
import DashboardView from '../views/DashboardView.vue';
import ChannelsView from '../views/ChannelsView.vue';
import LogsView from '../views/LogsView.vue';
import MemoryView from '../views/MemoryView.vue';
import MemorySearchView from '../views/MemorySearchView.vue';
import MemoryFilesView from '../views/MemoryFilesView.vue';
import OntologyView from '../views/OntologyView.vue';
import ProvidersView from '../views/ProvidersView.vue';
import WebSearchView from '../views/WebSearchView.vue';
import DiaryView from '../views/DiaryView.vue';
import GallivantingView from '../views/GallivantingView.vue';
import SchedulerView from '../views/SchedulerView.vue';
import ActiveMemoryView from '../views/ActiveMemoryView.vue';
import NodesView from '../views/NodesView.vue';
import SessionReportsView from '../views/SessionReportsView.vue';
import PromptLogsView from '../views/PromptLogsView.vue';
import LoginView from '../views/LoginView.vue';
import UsersView from '../views/UsersView.vue';
import DiffusionView from '../views/DiffusionView.vue';

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/login', name: 'login', component: LoginView, meta: { public: true } },
    { path: '/wizard', name: 'wizard', component: WizardView, meta: { public: true } },
    { path: '/', name: 'dashboard', component: DashboardView },
    { path: '/chat', name: 'chat', component: ChatView },
    { path: '/memory', name: 'memory', component: MemoryView },
    { path: '/memory-search', name: 'memory-search', component: MemorySearchView },
    { path: '/memory-files', name: 'memory-files', component: MemoryFilesView },
    { path: '/ontology', name: 'ontology', component: OntologyView },
    { path: '/scheduler', name: 'scheduler', component: SchedulerView },
    { path: '/providers', name: 'providers', component: ProvidersView },
    { path: '/web-search', name: 'web-search', component: WebSearchView },
    { path: '/agents', name: 'agents', component: AgentsView },
    { path: '/channels', name: 'channels', component: ChannelsView },
    { path: '/logs', name: 'logs', component: LogsView },
    { path: '/diary', name: 'diary', component: DiaryView },
    { path: '/gallivanting', name: 'gallivanting', component: GallivantingView },
    { path: '/active-memory', name: 'active-memory', component: ActiveMemoryView },
    { path: '/nodes', name: 'nodes', component: NodesView },
    { path: '/session-reports', name: 'session-reports', component: SessionReportsView },
    { path: '/prompt-logs', name: 'prompt-logs', component: PromptLogsView },
   { path: '/users', name: 'users', component: UsersView },
    { path: '/diffusion', name: 'diffusion', component: DiffusionView },
 ]
});

// Auth guard
import { useAuthStore } from '../stores/auth';
router.beforeEach(async (to, _from, next) => {
  const auth = useAuthStore();

  // Check auth status if we haven't yet (ref .value needed — not auto-unwrapped in plain JS)
  if (!auth.authRequired.value && !auth.token.value) {
    await auth.checkStatus();
  }

  // Public routes (login, wizard) always accessible
  if (to.meta.public) {
    next();
    return;
  }

  // If auth not required, allow
  if (!auth.authRequired.value) {
    next();
    return;
  }

  // If authenticated, allow
  if (auth.isAuthenticated.value) {
    next();
    return;
  }

  // Redirect to login
  next('/login');
});

export default router;
