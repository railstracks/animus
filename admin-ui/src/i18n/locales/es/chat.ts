export const chat = {
    sessionLabel: 'Sesión',
    newSession: 'Nueva sesión',
    refresh: 'Actualizar',
    stopGenerating: 'dejar de generar',
    emptyTitle: 'iniciar una conversación',
    emptyDescription: 'Pregúntale a Animus sobre la memoria, la configuración o una sesión en vivo para comenzar.',
    streamState: {
      streaming: 'transmisión...',
      stopped: 'detenido'
    },
    jumpToLatest: 'Saltar a lo último',
    composerPlaceholder: 'Enviar un mensaje a Animus...',
    adminTokenLabel: 'Token de administrador (opcional)',
    send: 'enviar',
    contextTitle: 'Contexto',
    context: {
      session: 'Sesión',
      layers: 'capas',
      tools: 'Herramientas',
      budget: 'Presupuesto',
      fallbackNote: 'La instantánea del contexto se actualiza a medida que cambian las sesiones.'
    },
    sidebarTabs: {
      context: 'Contexto',
      sessions: 'Sesiones',
      messages: 'mensajes',
      noSessions: 'Aún no hay sesiones disponibles.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'Redacta un mensaje para iniciar un nuevo hilo.'
    },
    thinking: {
      label: "Pensando"
    },
    toolResult: {
      label: "Resultado de la herramienta"
    },
    toolCall: {
      label: "Llamada de herramienta"
    },
    reasoning: {
      label: 'Modo de razonamiento',
      enabled: 'encendido',
      disabled: 'Apagado',
      effort: 'Esfuerzo',
      instructionLabel: 'Instrucción del sistema',
      instructionPlaceholder: 'Instrucción de razonamiento personalizada (usa el valor predeterminado si está vacío)',
      notSupported: 'El razonamiento no está disponible para este modelo.'
    },
    provider: {
      label: 'Proveedor',
      select: 'Proveedor',
      model: 'modelo'
    },
    agent: {
      label: 'Agente (nueva sesión)'
    },
    status: {
      closed: 'Cerrado',
      connecting: 'Conectando',
      open: 'Conectado'
    },
    errors: {
      websocket: 'Error de WebSocket',
      websocketNotConnected: 'WebSocket aún no está conectado. Por favor inténtalo de nuevo.',
      agentNotReady: 'La selección de agentes aún se está cargando. Espere un momento e inténtelo de nuevo.',
      unknownWebsocket: 'Error de websocket desconocido',
      sessionLoadFailed: 'No se pudo cargar la sesión {sessionId}: {message}'
    }
  } as const;
