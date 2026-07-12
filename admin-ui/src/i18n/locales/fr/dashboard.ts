export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'Bienvenue sur Animus.',
    welcomeSub: 'Configurez votre premier agent pour commencer.',
    beginSetup: 'Commencer la configuration →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'Temps de disponibilité',
    agents: 'Agents',
    sessions: 'Séances',
    providers: 'Fournisseurs',
    errors: 'euh',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'Séances récentes',
    viewAll: 'Voir tout',
    empty: 'Aucune séance pour l\'instant.',
    startChat: 'Démarrer une discussion →',
    messages: 'msgs',
  },
  // Memory card
  memory: {
    title: 'Mémoire',
    inspect: 'Inspecter',
    empty: 'Aucune couche de mémoire configurée.',
    totalObservations: 'observations totales',
  },
  // Provider card
  providers: {
    title: 'Fournisseurs',
    manage: 'Gérer',
    empty: 'Aucun fournisseur configuré.',
    runWizard: 'Exécuter l\'assistant de configuration →',
  },
  // Quick Actions card
  quickActions: {
    title: 'Actions rapides',
    newChat: 'Nouvelle discussion',
    addAgent: 'Ajouter un agent',
    provider: 'Fournisseur',
    logs: 'Journaux',
    scheduler: 'Planificateur',
    memory: 'Mémoire',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'Informations sur le démon',
    status: 'Statut',
    uptime: 'Temps de disponibilité',
    agents: 'Agents',
    activeSessions: 'Séances actives',
    providers: 'Fournisseurs',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'Couches de mémoire',
    empty: 'Aucune couche de mémoire configurée.',
  },
  // State labels
  state: {
    loading: 'chargement',
    unknown: 'inconnu',
  },
  // Error messages
  errors: {
    sessions: 'Échec du chargement des sessions',
    providers: 'Échec du chargement des fournisseurs',
    memory: 'API de mémoire indisponible',
  },
} as const;