// English locale for Animus Admin UI
// Reconstructed from compiled SPA + i18n/index.ts source

export default {
  app: {
    brand: {
      name: 'Animus',
      subtitle: 'Admin Interface'
    },
    toolbar: {
      title: 'Kernel Admin',
      languageLabel: 'Language'
    }
  },
  sidebar: {
    controlSurface: 'Control Surface',
    links: {
      dashboard: 'Dashboard',
      chat: 'Chat',
      memory: 'Memory',
      config: 'Configuration',
      interfaces: 'Interfaces',
      constitution: 'Constitution',
      logs: 'Logs'
    }
  },
  common: {
    unknownError: 'Unknown error',
    none: 'none',
    newSession: 'new'
  },
  dashboard: {
    cards: {
      systemStatus: 'System Status',
      recentActivity: 'Recent Activity'
    },
    state: {
      loading: 'loading',
      unknown: 'unknown',
      error: 'error'
    },
    labels: {
      state: 'State',
      uptimeSeconds: 'Uptime (s)'
    },
    placeholders: {
      recentActivity:
        'Placeholder summary. Ticket 010/011 will populate live session and memory activity.'
    }
  },
  chat: {
    sessionLabel: 'Session',
    newSession: 'New Session',
    refresh: 'Refresh',
    stopGenerating: 'Stop Generating',
    emptyTitle: 'Start a conversation',
    emptyDescription:
      'Ask Animus about memory, configuration, or a live session to get rolling.',
    streamState: {
      streaming: 'streaming...',
      stopped: 'stopped'
    },
    jumpToLatest: 'Jump To Latest',
    composerPlaceholder: 'Send a message to Animus...',
    adminTokenLabel: 'Admin Token (optional)',
    send: 'Send',
    contextTitle: 'Context',
    context: {
      session: 'Session',
      layers: 'Layers',
      tools: 'Tools',
      budget: 'Budget',
      fallbackNote: 'Context snapshot updates as sessions change.'
    },
    status: {
      closed: 'Closed',
      connecting: 'Connecting',
      open: 'Connected'
    },
    errors: {
      websocket: 'WebSocket error',
      websocketNotConnected: 'WebSocket is not connected yet. Please try again.',
      unknownWebsocket: 'Unknown websocket error',
      sessionLoadFailed: 'Failed to load session {sessionId}: {message}'
    }
  },
  memory: {
    title: 'Memory Browser',
    placeholder:
      'Memory layer explorer scaffold is in place. Detailed browsing and controls land in Ticket 011.',
    actions: {
      refresh: 'Refresh',
      triggerConsolidation: 'Trigger Consolidation'
    },
    common: {
      notAvailable: 'N/A'
    },
    consolidation: {
      title: 'Consolidation',
      activeJob: 'Active job',
      lastJob: 'Last job',
      lastRun: 'Last run',
      state: {
        idle: 'Idle',
        running: 'Running...'
      },
      promoted: 'promoted',
      demoted: 'demoted',
      archived: 'archived'
    },
    detail: {
      title: 'Observation Detail',
      id: 'ID',
      layer: 'Layer',
      content: 'Content',
      timestamp: 'Timestamp',
      createdInLayer: 'Created in layer',
      demotionReason: 'Demotion reason',
      demotionTimestamp: 'Demotion timestamp',
      editWeight: 'Weight',
      editTags: 'Tags (comma-separated)',
      save: 'Save',
      saveSuccess: 'Saved successfully',
      reset: 'Reset',
      archive: 'Archive',
      archiveSuccess: 'Archived successfully',
      empty: 'Select an observation to view details'
    },
    layers: {
      title: 'Memory Layers',
      empty: 'No layers configured',
      horizon: 'Horizon',
      consolidationInterval: 'Consolidation interval',
      retentionPolicy: 'Retention policy',
      perspectiveCurrent: 'current',
      perspectivePast: 'past',
      perspectiveFuture: 'future',
      viewObservations: 'View observations'
    },
    list: {
      title: 'Observations',
      activeLayer: 'Active layer',
      emptyLayer: 'No observations in this layer',
      emptyFilter: 'No observations match the filter',
      compactMode: 'Compact',
      pageSize: 'Per page',
      sortBy: 'Sort by',
      orderBy: 'Order',
      tagFilter: 'Filter by tag',
      openDetail: 'Open',
      columns: {
        content: 'Content',
        weight: 'Weight',
        age: 'Age',
        tags: 'Tags',
        decay: 'Decay',
        actions: 'Actions'
      },
      sort: {
        weight: 'Weight',
        age: 'Age',
        tags: 'Tags'
      },
      order: {
        asc: 'Ascending',
        desc: 'Descending'
      }
    }
  },
  config: {
    title: 'Agent Configuration',
    placeholder: 'Configuration panel scaffold ready for model/system prompt controls.'
  },
  interfaces: {
    title: 'Interface Management',
    placeholder: 'Connector management scaffold ready. Endpoint wiring exists from ticket 007.'
  },
  constitution: {
    title: 'Constitution Viewer',
    placeholder:
      'Constitution layer viewer scaffold ready. API endpoints are available from ticket 008.'
  },
  logs: {
    title: 'Logs',
    placeholder: 'Log viewer scaffold reserved for a future ticket.'
  }
};
