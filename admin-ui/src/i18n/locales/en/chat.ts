export const chat = {
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
    sidebarTabs: {
      context: 'Context',
      sessions: 'Sessions',
      messages: 'messages',
      noSessions: 'No sessions found.',
      newSessionHint: 'Compose a message to start a new thread.',
      searchSessions: 'Search sessions...'
    },
    thinking: {
      label: "Thinking"
    },
    toolResult: {
      label: "Tool Result"
    },
    toolCall: {
      label: "Tool Call"
    },
    reasoning: {
      label: 'Reasoning Mode',
      enabled: 'On',
      disabled: 'Off',
      effort: 'Effort',
      instructionLabel: 'System instruction',
      instructionPlaceholder: 'Custom reasoning instruction (uses default if empty)',
      notSupported: 'Reasoning is not available for this model.'
    },
    provider: {
      label: 'Provider',
      select: 'Provider',
      model: 'Model'
    },
    agent: {
      label: 'Agent (New Session)'
    },
    status: {
      closed: 'Closed',
      connecting: 'Connecting',
      open: 'Connected'
    },
    errors: {
      websocket: 'WebSocket error',
      websocketNotConnected: 'WebSocket is not connected yet. Please try again.',
      agentNotReady: 'Agent selection is still loading. Please wait a moment and try again.',
      unknownWebsocket: 'Unknown websocket error',
      sessionLoadFailed: 'Failed to load session {sessionId}: {message}'
    }
  } as const;
