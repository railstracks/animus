export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'Bienvenidos a Ánimus.',
    welcomeSub: 'Configure su primer agente para comenzar.',
    beginSetup: 'Comenzar la configuración →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'tiempo de actividad',
    agents: 'Agentes',
    sessions: 'Sesiones',
    providers: 'Proveedores',
    errors: 'errar',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'Sesiones recientes',
    viewAll: 'Ver todo',
    empty: 'Aún no hay sesiones.',
    startChat: 'Iniciar un chat →',
    messages: 'mensajes',
  },
  // Memory card
  memory: {
    title: 'Memoria',
    inspect: 'inspeccionar',
    empty: 'No hay capas de memoria configuradas.',
    totalObservations: 'observaciones totales',
  },
  // Provider card
  providers: {
    title: 'Proveedores',
    manage: 'Administrar',
    empty: 'No hay proveedores configurados.',
    runWizard: 'Ejecute el asistente de configuración →',
  },
  // Quick Actions card
  quickActions: {
    title: 'Acciones rápidas',
    newChat: 'Nuevo chat',
    addAgent: 'Agregar agente',
    provider: 'Proveedor',
    logs: 'Registros',
    scheduler: 'Programador',
    memory: 'Memoria',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'Información del demonio',
    status: 'Estado',
    uptime: 'tiempo de actividad',
    agents: 'Agentes',
    activeSessions: 'Sesiones activas',
    providers: 'Proveedores',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'Capas de memoria',
    empty: 'No hay capas de memoria configuradas.',
  },
  // State labels
  state: {
    loading: 'cargando',
    unknown: 'desconocido',
  },
  // Error messages
  errors: {
    sessions: 'No se pudieron cargar las sesiones',
    providers: 'No se pudieron cargar los proveedores',
    memory: 'API de memoria no disponible',
  },
} as const;