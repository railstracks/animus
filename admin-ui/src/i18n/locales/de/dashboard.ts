export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'Willkommen bei Animus.',
    welcomeSub: 'Richten Sie Ihren ersten Agenten ein, um loszulegen.',
    beginSetup: 'Beginnen Sie mit der Einrichtung →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'Betriebszeit',
    agents: 'Agenten',
    sessions: 'Sitzungen',
    providers: 'Anbieter',
    errors: 'ähm',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'Aktuelle Sitzungen',
    viewAll: 'Alle anzeigen',
    empty: 'Noch keine Sitzungen.',
    startChat: 'Starten Sie einen Chat →',
    messages: 'Nachrichten',
  },
  // Memory card
  memory: {
    title: 'Erinnerung',
    inspect: 'Inspizieren',
    empty: 'Keine Speicherschichten konfiguriert.',
    totalObservations: 'Gesamtbeobachtungen',
  },
  // Provider card
  providers: {
    title: 'Anbieter',
    manage: 'Verwalten',
    empty: 'Keine Anbieter konfiguriert.',
    runWizard: 'Führen Sie den Setup-Assistenten aus →',
  },
  // Quick Actions card
  quickActions: {
    title: 'Schnelle Aktionen',
    newChat: 'Neuer Chat',
    addAgent: 'Agent hinzufügen',
    provider: 'Anbieter',
    logs: 'Protokolle',
    scheduler: 'Planer',
    memory: 'Erinnerung',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'Daemon-Info',
    status: 'Status',
    uptime: 'Betriebszeit',
    agents: 'Agenten',
    activeSessions: 'Aktive Sitzungen',
    providers: 'Anbieter',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'Speicherschichten',
    empty: 'Keine Speicherschichten konfiguriert.',
  },
  // State labels
  state: {
    loading: 'Laden',
    unknown: 'unbekannt',
  },
  // Error messages
  errors: {
    sessions: 'Sitzungen konnten nicht geladen werden',
    providers: 'Anbieter konnten nicht geladen werden',
    memory: 'Speicher-API nicht verfügbar',
  },
} as const;