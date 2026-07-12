export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'Bem-vindo ao Animus.',
    welcomeSub: 'Configure seu primeiro agente para começar.',
    beginSetup: 'Comece a configuração →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'Tempo de atividade',
    agents: 'Agentes',
    sessions: 'Sessões',
    providers: 'Provedores',
    errors: 'errar',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'Sessões recentes',
    viewAll: 'Ver tudo',
    empty: 'Nenhuma sessão ainda.',
    startChat: 'Inicie um bate-papo →',
    messages: 'mensagens',
  },
  // Memory card
  memory: {
    title: 'Memória',
    inspect: 'Inspecionar',
    empty: 'Nenhuma camada de memória configurada.',
    totalObservations: 'observações totais',
  },
  // Provider card
  providers: {
    title: 'Provedores',
    manage: 'Gerenciar',
    empty: 'Nenhum provedor configurado.',
    runWizard: 'Execute o assistente de configuração →',
  },
  // Quick Actions card
  quickActions: {
    title: 'Ações rápidas',
    newChat: 'Novo bate-papo',
    addAgent: 'Adicionar agente',
    provider: 'Provedor',
    logs: 'Registros',
    scheduler: 'Agendador',
    memory: 'Memória',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'Informações do daemon',
    status: 'Estado',
    uptime: 'Tempo de atividade',
    agents: 'Agentes',
    activeSessions: 'Sessões ativas',
    providers: 'Provedores',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'Camadas de memória',
    empty: 'Nenhuma camada de memória configurada.',
  },
  // State labels
  state: {
    loading: 'carregando',
    unknown: 'desconhecido',
  },
  // Error messages
  errors: {
    sessions: 'Falha ao carregar sessões',
    providers: 'Falha ao carregar provedores',
    memory: 'API de memória indisponível',
  },
} as const;