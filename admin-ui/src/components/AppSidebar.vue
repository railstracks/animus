<script setup lang="ts">
import { computed, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useAuthStore } from '../stores/auth';
import { useRouter } from 'vue-router';

const { t } = useI18n();
const auth = useAuthStore();
const router = useRouter();

const links = [
  { titleKey: 'sidebar.links.dashboard', path: '/', icon: 'mdi-view-dashboard-outline' },
  { titleKey: 'sidebar.links.chat', path: '/chat', icon: 'mdi-chat-processing-outline' },
  { titleKey: 'sidebar.links.memorySearch', path: '/memory-search', icon: 'mdi-magnify' },
  { titleKey: 'sidebar.links.activeMemory', path: '/active-memory', icon: 'mdi-eye-outline' },
  { titleKey: 'sidebar.links.channels', path: '/channels', icon: 'mdi-connection' },
  { titleKey: 'sidebar.links.memory', path: '/memory', icon: 'mdi-brain' },
  { titleKey: 'sidebar.links.ontology', path: '/ontology', icon: 'mdi-graph-outline' },
  { titleKey: 'sidebar.links.memoryFiles', path: '/memory-files', icon: 'mdi-file-cabinet' },
  { titleKey: 'sidebar.links.scheduler', path: '/scheduler', icon: 'mdi-calendar-clock' },
  { titleKey: 'sidebar.links.providers', path: '/providers', icon: 'mdi-cloud-cog-outline' },
  { titleKey: 'sidebar.links.diffusion', path: '/diffusion', icon: 'mdi-creation' },
  { titleKey: 'sidebar.links.sops', path: '/sops', icon: 'mdi-book-open-page-variant' },
  { titleKey: 'sidebar.links.webSearch', path: '/web-search', icon: 'mdi-magnify' },
  { titleKey: 'sidebar.links.nodes', path: '/nodes', icon: 'mdi-lan' },
  { titleKey: 'sidebar.links.sessionReports', path: '/session-reports', icon: 'mdi-file-document-multiple-outline' },
  { titleKey: 'sidebar.links.promptLogs', path: '/prompt-logs', icon: 'mdi-chart-line-variant' },
  { titleKey: 'sidebar.links.agents', path: '/agents', icon: 'mdi-account-group' },
  { titleKey: 'sidebar.links.gallivanting', path: '/gallivanting', icon: 'mdi-walk' },
  { titleKey: 'sidebar.links.diary', path: '/diary', icon: 'mdi-notebook' },
  { titleKey: 'sidebar.links.logs', path: '/logs', icon: 'mdi-text-box-search-outline' },
  { titleKey: 'sidebar.links.users', path: '/users', icon: 'mdi-account-cog-outline' },
] as const;

const isAdmin = computed(() => {
  // Static token (no username) = admin. Session user needs role === 'admin'.
  return !auth.username.value || auth.role.value === 'admin';
});

const translatedLinks = computed(() =>
  links
    .filter((link) => {
      // Hide admin-only pages from non-admin users
      if (!isAdmin.value) {
        const adminPaths = ['/channels', '/providers', '/users', '/diffusion'];
        if (adminPaths.includes(link.path)) return false;
      }
      return true;
    })
    .map((link) => ({
      ...link,
      title: t(link.titleKey)
    }))
);
</script>

<template>
  <v-list nav density="comfortable">
    <v-list-subheader class="subheader">{{ t('sidebar.controlSurface') }}</v-list-subheader>
    <v-list-item
      v-for="link in translatedLinks"
      :key="link.path"
      :to="link.path"
      rounded="lg"
      active-class="active-nav-item"
      :prepend-icon="link.icon"
      :title="link.title"
    />
  </v-list>

  <v-list nav density="comfortable" v-if="auth.authRequired">
    <v-list-subheader class="subheader">Account</v-list-subheader>
    <v-list-item v-if="auth.username" prepend-icon="mdi-account" :title="auth.username" disabled />
    <v-list-item
      prepend-icon="mdi-logout"
      title="Sign Out"
      @click="auth.logout(); router.push('/login')"
      rounded="lg"
    />
  </v-list>
</template>

<style scoped>
.subheader {
  text-transform: uppercase;
  letter-spacing: 0.08em;
  opacity: 0.75;
}
</style>
