import { createRouter, createWebHistory } from 'vue-router';

import AgentsView from '../views/AgentsView.vue';
import ChatView from '../views/ChatView.vue';
import WizardView from '../views/WizardView.vue';
import ConstitutionView from '../views/ConstitutionView.vue';
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

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/wizard', name: 'wizard', component: WizardView },
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
    { path: '/constitution', name: 'constitution', component: ConstitutionView },
    { path: '/logs', name: 'logs', component: LogsView },
    { path: '/diary', name: 'diary', component: DiaryView },
    { path: '/gallivanting', name: 'gallivanting', component: GallivantingView },
    { path: '/active-memory', name: 'active-memory', component: ActiveMemoryView },
    { path: '/nodes', name: 'nodes', component: NodesView },
    { path: '/session-reports', name: 'session-reports', component: SessionReportsView },
    { path: '/prompt-logs', name: 'prompt-logs', component: PromptLogsView }
  ]
});

export default router;
