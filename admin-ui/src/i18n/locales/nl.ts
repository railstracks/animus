// Dutch locale for Animus Admin UI
// Reconstructed from compiled SPA + i18n key inventory

export default {
  app: {
    brand: {
      name: 'Animus',
      subtitle: 'Beheerinterface'
    },
    toolbar: {
      title: 'Kernel Beheer',
      languageLabel: 'Taal'
    }
  },
  sidebar: {
    controlSurface: 'Bedieningspaneel',
    links: {
      dashboard: 'Dashboard',
      chat: 'Chat',
      memory: 'Geheugen',
      config: 'Configuratie',
      interfaces: 'Interfaces',
      constitution: 'Constitutie',
      logs: 'Logboek'
    }
  },
  common: {
    unknownError: 'Onbekende fout',
    none: 'geen',
    newSession: 'nieuw'
  },
  dashboard: {
    cards: {
      systemStatus: 'Systeemstatus',
      recentActivity: 'Recente Activiteit'
    },
    state: {
      loading: 'laden',
      unknown: 'onbekend',
      error: 'fout'
    },
    labels: {
      state: 'Status',
      uptimeSeconds: 'Uptime (s)'
    },
    placeholders: {
      recentActivity:
        'Tijdelijke samenvatting. Ticket 010/011 vult live sessie- en geheugenactiviteit in.'
    }
  },
  chat: {
    sessionLabel: 'Sessie',
    newSession: 'Nieuwe Sessie',
    refresh: 'Verversen',
    stopGenerating: 'Stoppen met genereren',
    emptyTitle: 'Start een gesprek',
    emptyDescription:
      'Vraag Animus over geheugen, configuratie, of een live sessie om te beginnen.',
    streamState: {
      streaming: 'streamen...',
      stopped: 'gestopt'
    },
    jumpToLatest: 'Spring naar laatste',
    composerPlaceholder: 'Stuur een bericht aan Animus...',
    adminTokenLabel: 'Admin Token (optioneel)',
    send: 'Verzenden',
    contextTitle: 'Context',
    context: {
      session: 'Sessie',
      layers: 'Lagen',
      tools: 'Tools',
      budget: 'Budget',
      fallbackNote: 'Contextsnapshot wordt bijgewerkt naarmate sessies veranderen.'
    },
    status: {
      closed: 'Gesloten',
      connecting: 'Verbinden',
      open: 'Verbonden'
    },
    errors: {
      websocket: 'WebSocket-fout',
      websocketNotConnected: 'WebSocket is nog niet verbonden. Probeer opnieuw.',
      unknownWebsocket: 'Onbekende WebSocket-fout',
      sessionLoadFailed: 'Kan sessie {sessionId} niet laden: {message}'
    }
  },
  memory: {
    title: 'Geheugenbrowser',
    placeholder:
      'Geheugenlaag-verkenner is opgezet. Gedetailleerd browsen en besturing volgen in Ticket 011.',
    actions: {
      refresh: 'Verversen',
      triggerConsolidation: 'Consolidatie starten'
    },
    common: {
      notAvailable: 'n.v.t.'
    },
    consolidation: {
      title: 'Consolidatie',
      activeJob: 'Actieve taak',
      lastJob: 'Laatste taak',
      lastRun: 'Laatste uitvoering',
      state: {
        idle: 'Inactief',
        running: 'Bezig...'
      },
      promoted: 'gepromoveerd',
      demoted: 'gedegradeerd',
      archived: 'gearchiveerd'
    },
    detail: {
      title: 'Observatiedetail',
      id: 'ID',
      layer: 'Laag',
      content: 'Inhoud',
      timestamp: 'Tijdstempel',
      createdInLayer: 'Aangemaakt in laag',
      demotionReason: 'Degraderingsreden',
      demotionTimestamp: 'Degraderingstijdstempel',
      editWeight: 'Gewicht',
      editTags: 'Tags (kommagescheiden)',
      save: 'Opslaan',
      saveSuccess: 'Opgeslagen',
      reset: 'Resetten',
      archive: 'Archiveren',
      archiveSuccess: 'Gearchiveerd',
      empty: 'Selecteer een observatie om details te bekijken'
    },
    layers: {
      title: 'Geheugenlagen',
      empty: 'Geen lagen geconfigureerd',
      horizon: 'Horizon',
      consolidationInterval: 'Consolidatie-interval',
      retentionPolicy: 'Retentiebeleid',
      perspectiveCurrent: 'huidig',
      perspectivePast: 'verleden',
      perspectiveFuture: 'toekomst',
      viewObservations: 'Observaties bekijken'
    },
    list: {
      title: 'Observaties',
      activeLayer: 'Actieve laag',
      emptyLayer: 'Geen observaties in deze laag',
      emptyFilter: 'Geen observaties komen overeen met het filter',
      compactMode: 'Compact',
      pageSize: 'Per pagina',
      sortBy: 'Sorteren op',
      orderBy: 'Volgorde',
      tagFilter: 'Filteren op tag',
      openDetail: 'Openen',
      columns: {
        content: 'Inhoud',
        weight: 'Gewicht',
        age: 'Leeftijd',
        tags: 'Tags',
        decay: 'Verval',
        actions: 'Acties'
      },
      sort: {
        weight: 'Gewicht',
        age: 'Leeftijd',
        tags: 'Tags'
      },
      order: {
        asc: 'Oplopend',
        desc: 'Aflopend'
      }
    }
  },
  config: {
    title: 'Agentconfiguratie',
    placeholder: 'Configuratiepaneel in opbouw. Model- en systeemprompt-besturing volgen.'
  },
  interfaces: {
    title: 'Interfacebeheer',
    placeholder: 'Connectorbeheer in opbouw. Endpoint-koppeling beschikbaar vanuit ticket 007.'
  },
  constitution: {
    title: 'Constitutieviewer',
    placeholder:
      'Constitutielaag-viewer in opbouw. API-eindpunten beschikbaar vanuit ticket 008.'
  },
  logs: {
    title: 'Logboek',
    placeholder: 'Logviewer gereserveerd voor een toekomstige ticket.'
  }
};
