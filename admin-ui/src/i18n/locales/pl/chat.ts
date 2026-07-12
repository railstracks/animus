export const chat = {
    sessionLabel: 'Sesja',
    newSession: 'Nowa sesja',
    refresh: 'Odśwież',
    stopGenerating: 'Przestań generować',
    emptyTitle: 'Rozpocznij rozmowę',
    emptyDescription: 'Zapytaj Animusa o pamięć, konfigurację lub sesję na żywo, aby rozpocząć grę.',
    streamState: {
      streaming: 'przesyłanie strumieniowe...',
      stopped: 'zatrzymał się'
    },
    jumpToLatest: 'Przejdź do najnowszych',
    composerPlaceholder: 'Wyślij wiadomość do Animusa...',
    adminTokenLabel: 'Token administratora (opcjonalnie)',
    send: 'Wyślij',
    contextTitle: 'Kontekst',
    context: {
      session: 'Sesja',
      layers: 'Warstwy',
      tools: 'Narzędzia',
      budget: 'Budżet',
      fallbackNote: 'Aktualizacje migawek kontekstu w miarę zmiany sesji.'
    },
    sidebarTabs: {
      context: 'Kontekst',
      sessions: 'Sesje',
      messages: 'wiadomości',
      noSessions: 'Brak dostępnych sesji.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'Utwórz wiadomość, aby rozpocząć nowy wątek.'
    },
    thinking: {
      label: "Myślenie"
    },
    toolResult: {
      label: "Wynik narzędzia"
    },
    toolCall: {
      label: "Wezwanie narzędzia"
    },
    reasoning: {
      label: 'Tryb rozumowania',
      enabled: 'Włączone',
      disabled: 'Wyłączone',
      effort: 'Wysiłek',
      instructionLabel: 'Instrukcja systemowa',
      instructionPlaceholder: 'Niestandardowa instrukcja rozumowania (używa wartości domyślnej, jeśli jest pusta)',
      notSupported: 'W przypadku tego modelu rozumowanie nie jest dostępne.'
    },
    provider: {
      label: 'Dostawca',
      select: 'Dostawca',
      model: 'Modelka'
    },
    agent: {
      label: 'Agent (nowa sesja)'
    },
    status: {
      closed: 'Zamknięte',
      connecting: 'Łączenie',
      open: 'Połączono'
    },
    errors: {
      websocket: 'Błąd protokołu WebSocket',
      websocketNotConnected: 'WebSocket nie jest jeszcze podłączony. Spróbuj ponownie.',
      agentNotReady: 'Wybór agenta nadal się ładuje. Poczekaj chwilę i spróbuj ponownie.',
      unknownWebsocket: 'Nieznany błąd protokołu internetowego',
      sessionLoadFailed: 'Nie udało się załadować sesji {sessionId}: {message}'
    }
  } as const;
