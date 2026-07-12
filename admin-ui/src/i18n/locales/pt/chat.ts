export const chat = {
    sessionLabel: 'Sessão',
    newSession: 'Nova sessão',
    refresh: 'Atualizar',
    stopGenerating: 'Pare de gerar',
    emptyTitle: 'Inicie uma conversa',
    emptyDescription: 'Pergunte ao Animus sobre memória, configuração ou uma sessão ao vivo para começar.',
    streamState: {
      streaming: 'streaming...',
      stopped: 'parou'
    },
    jumpToLatest: 'Ir para o mais recente',
    composerPlaceholder: 'Envie uma mensagem para Animus...',
    adminTokenLabel: 'Token de administrador (opcional)',
    send: 'Enviar',
    contextTitle: 'Contexto',
    context: {
      session: 'Sessão',
      layers: 'Camadas',
      tools: 'Ferramentas',
      budget: 'Orçamento',
      fallbackNote: 'O instantâneo de contexto é atualizado conforme as sessões mudam.'
    },
    sidebarTabs: {
      context: 'Contexto',
      sessions: 'Sessões',
      messages: 'mensagens',
      noSessions: 'Nenhuma sessão disponível ainda.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'Escreva uma mensagem para iniciar um novo tópico.'
    },
    thinking: {
      label: "Pensando"
    },
    toolResult: {
      label: "Resultado da ferramenta"
    },
    toolCall: {
      label: "Chamada de ferramenta"
    },
    reasoning: {
      label: 'Modo de raciocínio',
      enabled: 'Ligado',
      disabled: 'Desligado',
      effort: 'Esforço',
      instructionLabel: 'Instrução do sistema',
      instructionPlaceholder: 'Instrução de raciocínio personalizada (usa padrão se estiver vazia)',
      notSupported: 'O raciocínio não está disponível para este modelo.'
    },
    provider: {
      label: 'Provedor',
      select: 'Provedor',
      model: 'Modelo'
    },
    agent: {
      label: 'Agente (Nova Sessão)'
    },
    status: {
      closed: 'Fechado',
      connecting: 'Conectando',
      open: 'Conectado'
    },
    errors: {
      websocket: 'Erro WebSocket',
      websocketNotConnected: 'WebSocket ainda não está conectado. Por favor, tente novamente.',
      agentNotReady: 'A seleção do agente ainda está sendo carregada. Aguarde um momento e tente novamente.',
      unknownWebsocket: 'Erro de websocket desconhecido',
      sessionLoadFailed: 'Falha ao carregar a sessão {sessionId}: {message}'
    }
  } as const;
