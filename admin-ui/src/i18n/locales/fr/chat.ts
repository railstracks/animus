export const chat = {
    sessionLabel: 'Séance',
    newSession: 'Nouvelle séance',
    refresh: 'Actualiser',
    stopGenerating: 'Arrêter de générer',
    emptyTitle: 'Démarrer une conversation',
    emptyDescription: 'Interrogez Animus sur la mémoire, la configuration ou une session en direct pour commencer.',
    streamState: {
      streaming: 'en streaming...',
      stopped: 'arrêté'
    },
    jumpToLatest: 'Aller au dernier',
    composerPlaceholder: 'Envoyer un message à Animus...',
    adminTokenLabel: 'Jeton d\'administrateur (facultatif)',
    send: 'Envoyer',
    contextTitle: 'Contexte',
    context: {
      session: 'Séance',
      layers: 'Calques',
      tools: 'Outils',
      budget: 'Budget',
      fallbackNote: 'L\'instantané de contexte est mis à jour à mesure que les sessions changent.'
    },
    sidebarTabs: {
      context: 'Contexte',
      sessions: 'Séances',
      messages: 'messages',
      noSessions: 'Aucune séance disponible pour l\'instant.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'Composez un message pour démarrer un nouveau fil de discussion.'
    },
    thinking: {
      label: "Penser"
    },
    toolResult: {
      label: "Résultat de l'outil"
    },
    toolCall: {
      label: "Appel d'outil"
    },
    reasoning: {
      label: 'Mode de raisonnement',
      enabled: 'Sur',
      disabled: 'Désactivé',
      effort: 'Effort',
      instructionLabel: 'Instruction système',
      instructionPlaceholder: 'Instruction de raisonnement personnalisé (utilise la valeur par défaut si vide)',
      notSupported: 'Le raisonnement n’est pas disponible pour ce modèle.'
    },
    provider: {
      label: 'Fournisseur',
      select: 'Fournisseur',
      model: 'Modèle'
    },
    agent: {
      label: 'Agent (nouvelle session)'
    },
    status: {
      closed: 'Fermé',
      connecting: 'Connexion',
      open: 'Connecté'
    },
    errors: {
      websocket: 'Erreur WebSocket',
      websocketNotConnected: 'WebSocket n\'est pas encore connecté. Veuillez réessayer.',
      agentNotReady: 'La sélection d\'agent est toujours en cours de chargement. Veuillez patienter un moment et réessayer.',
      unknownWebsocket: 'Erreur Websocket inconnue',
      sessionLoadFailed: 'Échec du chargement de la session {sessionId} : {message}'
    }
  } as const;
