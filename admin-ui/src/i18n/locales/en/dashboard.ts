export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'Welcome to Animus.',
    welcomeSub: 'Set up your first agent to get started.',
    beginSetup: 'Begin Setup →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'Uptime',
    agents: 'Agents',
    sessions: 'Sessions',
    providers: 'Providers',
    errors: 'err',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'Recent Sessions',
    viewAll: 'View all',
    empty: 'No sessions yet.',
    startChat: 'Start a chat →',
    messages: 'msgs',
  },
  // Memory card
  memory: {
    title: 'Memory',
    inspect: 'Inspect',
    empty: 'No memory layers configured.',
    totalObservations: 'total observations',
  },
  // Provider card
  providers: {
    title: 'Providers',
    manage: 'Manage',
    empty: 'No providers configured.',
    runWizard: 'Run setup wizard →',
  },
  // Quick Actions card
  quickActions: {
    title: 'Quick Actions',
    newChat: 'New Chat',
    addAgent: 'Add Agent',
    provider: 'Provider',
    logs: 'Logs',
    scheduler: 'Scheduler',
    memory: 'Memory',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'Daemon Info',
    status: 'Status',
    uptime: 'Uptime',
    agents: 'Agents',
    activeSessions: 'Active sessions',
    providers: 'Providers',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'Memory Layers',
    empty: 'No memory layers configured.',
  },
  // State labels
  state: {
    loading: 'loading',
    unknown: 'unknown',
  },
  // Error messages
  errors: {
    sessions: 'Failed to load sessions',
    providers: 'Failed to load providers',
    memory: 'Memory API unavailable',
  },
} as const;