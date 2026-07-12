export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'Barka da zuwa Animus.',
    welcomeSub: 'Saita wakilin ku na farko don farawa.',
    beginSetup: 'Fara Saita →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'Uptime',
    agents: 'Wakilai',
    sessions: 'Zama',
    providers: 'Masu bayarwa',
    errors: 'kuskure',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'Zaman Kwanan nan',
    viewAll: 'Duba duka',
    empty: 'Babu zaman tukuna.',
    startChat: 'Fara hira →',
    messages: 'msgs',
  },
  // Memory card
  memory: {
    title: 'Ƙwaƙwalwar ajiya',
    inspect: 'Duba',
    empty: 'Ba a saita matakan ƙwaƙwalwar ajiya ba.',
    totalObservations: 'jimlar lura',
  },
  // Provider card
  providers: {
    title: 'Masu bayarwa',
    manage: 'Sarrafa',
    empty: 'Babu masu samar da da aka saita.',
    runWizard: 'Run saitin maye →',
  },
  // Quick Actions card
  quickActions: {
    title: 'Ayyukan gaggawa',
    newChat: 'Sabuwar Taɗi',
    addAgent: 'Ƙara Wakili',
    provider: 'Mai bayarwa',
    logs: 'Logs',
    scheduler: 'Mai tsara jadawalin',
    memory: 'Ƙwaƙwalwar ajiya',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'Daemon bayanai',
    status: 'Matsayi',
    uptime: 'Uptime',
    agents: 'Wakilai',
    activeSessions: 'Zaman aiki',
    providers: 'Masu bayarwa',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'Ƙwaƙwalwar ajiya',
    empty: 'Ba a saita matakan ƙwaƙwalwar ajiya ba.',
  },
  // State labels
  state: {
    loading: 'lodi',
    unknown: 'wanda ba a sani ba',
  },
  // Error messages
  errors: {
    sessions: 'An kasa loda zaman',
    providers: 'An kasa loda masu samarwa',
    memory: 'API ɗin ƙwaƙwalwar ajiya babu',
  },
} as const;