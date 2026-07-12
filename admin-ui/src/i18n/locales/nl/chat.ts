export const chat = {
    sessionLabel: 'Sessie',
    newSession: 'Nieuwe sessie',
    refresh: 'Vernieuwen',
    stopGenerating: 'Genereren stoppen',
    emptyTitle: 'Start een gesprek',
    emptyDescription: 'Vraag Animus naar geheugen, configuratie of een live sessie om te beginnen.',
    streamState: {
      streaming: 'streamen...',
      stopped: 'gestopt'
    },
    jumpToLatest: 'Naar nieuwste springen',
    composerPlaceholder: 'Stuur een bericht naar Animus...',
    adminTokenLabel: 'Beheertoken (optioneel)',
    send: 'Versturen',
    contextTitle: 'Context',
    context: {
      session: 'Sessie',
      layers: 'Lagen',
      tools: 'Tools',
      budget: 'Budget',
      fallbackNote: 'Contextsnapshot wordt bijgewerkt wanneer sessies veranderen.'
    },
    sidebarTabs: {
      context: 'Context',
      sessions: 'Sessies',
      messages: 'berichten',
      noSessions: 'Er zijn nog geen sessies beschikbaar.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'Stel een bericht op om een nieuwe thread te starten.'
    },
    thinking: {
      label: "Denkt na"
    },
    toolResult: {
      label: "Toolresultaat"
    },
    toolCall: {
      label: "Toolaanroep"
    },
    reasoning: {
      label: 'Redeneermodus',
      enabled: 'Aan',
      disabled: 'Uit',
      effort: 'Inspanning',
      instructionLabel: 'Systeeminstructie',
      instructionPlaceholder: 'Aangepaste redeneerinstructie (gebruikt standaardwaarde indien leeg)',
      notSupported: 'Redeneren is niet beschikbaar voor dit model.'
    },
    provider: {
      label: 'Provider',
      select: 'Provider',
      model: 'Model'
    },
    agent: {
      label: 'Agent (nieuwe sessie)'
    },
    status: {
      closed: 'Gesloten',
      connecting: 'Verbinden',
      open: 'Verbonden'
    },
    errors: {
      websocket: 'WebSocket-fout',
      websocketNotConnected: 'WebSocket is nog niet verbonden. Probeer het opnieuw.',
      agentNotReady: 'Agentselectie wordt nog geladen. Wacht even en probeer het opnieuw.',
      unknownWebsocket: 'Onbekende WebSocket-fout',
      sessionLoadFailed: 'Laden van sessie {sessionId} mislukt: {message}'
    }
  } as const;
