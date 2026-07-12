export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'Witamy w Animusie.',
    welcomeSub: 'Aby rozpocząć, skonfiguruj pierwszego agenta.',
    beginSetup: 'Rozpocznij konfigurację →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'Czas pracy',
    agents: 'Agenci',
    sessions: 'Sesje',
    providers: 'Dostawcy',
    errors: 'błąd',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'Ostatnie sesje',
    viewAll: 'Zobacz wszystkie',
    empty: 'Nie ma jeszcze sesji.',
    startChat: 'Rozpocznij czat →',
    messages: 'wiadomości',
  },
  // Memory card
  memory: {
    title: 'Pamięć',
    inspect: 'Sprawdź',
    empty: 'Nie skonfigurowano żadnych warstw pamięci.',
    totalObservations: 'totalne obserwacje',
  },
  // Provider card
  providers: {
    title: 'Dostawcy',
    manage: 'Zarządzaj',
    empty: 'Nie skonfigurowano żadnych dostawców.',
    runWizard: 'Uruchom kreatora instalacji →',
  },
  // Quick Actions card
  quickActions: {
    title: 'Szybkie działania',
    newChat: 'Nowy czat',
    addAgent: 'Dodaj agenta',
    provider: 'Dostawca',
    logs: 'Dzienniki',
    scheduler: 'Harmonogram',
    memory: 'Pamięć',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'Informacje o demonach',
    status: 'Stan',
    uptime: 'Czas pracy',
    agents: 'Agenci',
    activeSessions: 'Aktywne sesje',
    providers: 'Dostawcy',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'Warstwy pamięci',
    empty: 'Nie skonfigurowano żadnych warstw pamięci.',
  },
  // State labels
  state: {
    loading: 'ładowanie',
    unknown: 'nieznany',
  },
  // Error messages
  errors: {
    sessions: 'Nie udało się załadować sesji',
    providers: 'Nie udało się załadować dostawców',
    memory: 'Interfejs API pamięci jest niedostępny',
  },
} as const;