export const chat = {
    sessionLabel: 'Sitzung',
    newSession: 'Neue Sitzung',
    refresh: 'Aktualisieren',
    stopGenerating: 'Stoppen Sie die Generierung',
    emptyTitle: 'Beginnen Sie ein Gespräch',
    emptyDescription: 'Fragen Sie Animus nach Speicher, Konfiguration oder einer Live-Sitzung, um loszulegen.',
    streamState: {
      streaming: 'Streaming...',
      stopped: 'gestoppt'
    },
    jumpToLatest: 'Zum Neuesten springen',
    composerPlaceholder: 'Senden Sie eine Nachricht an Animus...',
    adminTokenLabel: 'Admin-Token (optional)',
    send: 'Senden',
    contextTitle: 'Kontext',
    context: {
      session: 'Sitzung',
      layers: 'Schichten',
      tools: 'Werkzeuge',
      budget: 'Budget',
      fallbackNote: 'Kontext-Snapshot-Updates, wenn sich Sitzungen ändern.'
    },
    sidebarTabs: {
      context: 'Kontext',
      sessions: 'Sitzungen',
      messages: 'Nachrichten',
      noSessions: 'Noch keine Sitzungen verfügbar.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'Verfassen Sie eine Nachricht, um einen neuen Thread zu starten.'
    },
    thinking: {
      label: "Denken"
    },
    toolResult: {
      label: "Werkzeugergebnis"
    },
    toolCall: {
      label: "Werkzeugaufruf"
    },
    reasoning: {
      label: 'Argumentationsmodus',
      enabled: 'Auf',
      disabled: 'Aus',
      effort: 'Aufwand',
      instructionLabel: 'Systemanweisung',
      instructionPlaceholder: 'Benutzerdefinierte Argumentationsanweisung (verwendet den Standardwert, wenn leer)',
      notSupported: 'Für dieses Modell ist keine Begründung verfügbar.'
    },
    provider: {
      label: 'Anbieter',
      select: 'Anbieter',
      model: 'Modell'
    },
    agent: {
      label: 'Agent (Neue Sitzung)'
    },
    status: {
      closed: 'Geschlossen',
      connecting: 'Verbinden',
      open: 'Verbunden'
    },
    errors: {
      websocket: 'WebSocket-Fehler',
      websocketNotConnected: 'WebSocket ist noch nicht verbunden. Bitte versuchen Sie es erneut.',
      agentNotReady: 'Die Agentenauswahl wird noch geladen. Bitte warten Sie einen Moment und versuchen Sie es erneut.',
      unknownWebsocket: 'Unbekannter WebSocket-Fehler',
      sessionLoadFailed: 'Sitzung {sessionId} konnte nicht geladen werden: {message}'
    }
  } as const;
