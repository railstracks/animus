export const chat = {
    sessionLabel: 'Zama',
    newSession: 'Sabon Zama',
    refresh: 'Sake sabuntawa',
    stopGenerating: 'Dakatar da Ƙarfafawa',
    emptyTitle: 'Fara tattaunawa',
    emptyDescription: 'Tambayi Animus game da ƙwaƙwalwar ajiya, daidaitawa, ko zama mai rai don yin birgima.',
    streamState: {
      streaming: 'yawo...',
      stopped: 'tsaya'
    },
    jumpToLatest: 'Tsallaka Zuwa Bugawa',
    composerPlaceholder: 'Aika sako zuwa Animus...',
    adminTokenLabel: 'Admin Token (na zaɓi)',
    send: 'Aika',
    contextTitle: 'Magana',
    context: {
      session: 'Zama',
      layers: 'Yadudduka',
      tools: 'Kayan aiki',
      budget: 'Kasafin kudi',
      fallbackNote: 'Sabunta hoton yanayi yayin da zaman ke canzawa.'
    },
    sidebarTabs: {
      context: 'Magana',
      sessions: 'Zama',
      messages: 'saƙonni',
      noSessions: 'Babu zaman da aka samu tukuna.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'Shirya saƙo don fara sabon zaren.'
    },
    thinking: {
      label: "Tunani"
    },
    toolResult: {
      label: "Sakamakon Kayan aiki"
    },
    toolCall: {
      label: "Kiran Kayan aiki"
    },
    reasoning: {
      label: 'Yanayin Hankali',
      enabled: 'Kunna',
      disabled: 'Kashe',
      effort: 'Ƙoƙari',
      instructionLabel: 'Umarnin tsarin',
      instructionPlaceholder: 'Umarnin tunani na al\'ada (yana amfani da tsoho idan babu komai)',
      notSupported: 'Babu dalili don wannan ƙirar.'
    },
    provider: {
      label: 'Mai bayarwa',
      select: 'Mai bayarwa',
      model: 'Samfura'
    },
    agent: {
      label: 'Wakili (Sabon Zama)'
    },
    status: {
      closed: 'An rufe',
      connecting: 'Haɗawa',
      open: 'An haɗa'
    },
    errors: {
      websocket: 'Kuskuren WebSocket',
      websocketNotConnected: 'Ba a haɗa WebSocket ba tukuna. Da fatan za a sake gwadawa.',
      agentNotReady: 'Har yanzu ana loda zaɓin wakili. Da fatan za a jira ɗan lokaci kuma a sake gwadawa.',
      unknownWebsocket: 'Ba a sani ba kuskuren websocket',
      sessionLoadFailed: 'An kasa loda zaman {sessionId}: {message}'
    }
  } as const;
