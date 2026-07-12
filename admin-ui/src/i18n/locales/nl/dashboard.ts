export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'Welkom bij Animus.',
    welcomeSub: 'Stel je eerste agent in om aan de slag te gaan.',
    beginSetup: 'Installatie starten →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'Uptime',
    agents: 'Agenten',
    sessions: 'Sessies',
    providers: 'Providers',
    errors: 'fouten',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'Recente sessies',
    viewAll: 'Alles bekijken',
    empty: 'Nog geen sessies.',
    startChat: 'Start een chat →',
    messages: 'ber.',
  },
  // Memory card
  memory: {
    title: 'Geheugen',
    inspect: 'Bekijken',
    empty: 'Geen geheugenlagen geconfigureerd.',
    totalObservations: 'totaal aantal observaties',
  },
  // Provider card
  providers: {
    title: 'Providers',
    manage: 'Beheren',
    empty: 'Geen providers geconfigureerd.',
    runWizard: 'Installatiewizard starten →',
  },
  // Quick Actions card
  quickActions: {
    title: 'Snelle acties',
    newChat: 'Nieuwe chat',
    addAgent: 'Agent toevoegen',
    provider: 'Provider',
    logs: 'Logboeken',
    scheduler: 'Planner',
    memory: 'Geheugen',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'Daemoninformatie',
    status: 'Status',
    uptime: 'Uptime',
    agents: 'Agenten',
    activeSessions: 'Actieve sessies',
    providers: 'Providers',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'Geheugenlagen',
    empty: 'Geen geheugenlagen geconfigureerd.',
  },
  // State labels
  state: {
    loading: 'laden',
    unknown: 'onbekend',
  },
  // Error messages
  errors: {
    sessions: 'Laden van sessies mislukt',
    providers: 'Laden van providers mislukt',
    memory: 'Geheugen-API niet beschikbaar',
  },
} as const;