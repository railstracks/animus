<script setup lang="ts">
const links = [
  { title: 'Dashboard', path: '/', icon: 'mdi-view-dashboard-outline' },
  { title: 'Chat', path: '/chat', icon: 'mdi-chat-processing-outline' },
  { title: 'Memory', path: '/memory', icon: 'mdi-brain' },
  { title: 'Configuration', path: '/config', icon: 'mdi-tune-variant' },
  { title: 'Interfaces', path: '/interfaces', icon: 'mdi-connection' },
  { title: 'Constitution', path: '/constitution', icon: 'mdi-scale-balance' },
  { title: 'Logs', path: '/logs', icon: 'mdi-text-box-search-outline' }
] as const;
</script>

<template>
  <v-list nav density="comfortable">
    <v-list-subheader class="subheader">Control Surface</v-list-subheader>
    <v-list-item
      v-for="link in links"
      :key="link.path"
      :to="link.path"
      rounded="lg"
      active-class="active-nav-item"
      :prepend-icon="link.icon"
      :title="link.title"
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
