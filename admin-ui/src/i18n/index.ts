import { createI18n } from 'vue-i18n';

const messages = {
  en: {
    app: {
      brand: {
        name: 'Animus',
        subtitle: 'Admin Interface'
      },
      toolbar: {
        title: 'Kernel Admin'
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
      emptyDescription: 'Ask Animus about memory, configuration, or a live session to get rolling.',
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
        'Memory layer explorer scaffold is in place. Detailed browsing and controls land in Ticket 011.'
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
      placeholder: 'Constitution layer viewer scaffold ready. API endpoints are available from ticket 008.'
    },
    logs: {
      title: 'Logs',
      placeholder: 'Log viewer scaffold reserved for a future ticket.'
    }
  }
} as const;

type SupportedLocale = keyof typeof messages;

function resolveInitialLocale(): SupportedLocale {
  const stored =
    typeof localStorage !== 'undefined' ? localStorage.getItem('animus_admin_locale') : null;
  if (stored === 'en') {
    return stored;
  }
  return 'en';
}

export const i18n = createI18n({
  legacy: false,
  locale: resolveInitialLocale(),
  fallbackLocale: 'en',
  messages
});
