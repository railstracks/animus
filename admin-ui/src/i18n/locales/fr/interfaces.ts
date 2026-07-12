export const interfaces = {
    title: 'Gestion des interfaces',
    actions: {
      refresh: 'Actualiser',
      add: 'Ajouter une interface',
      edit: 'Modifier',
      enable: 'Activer',
      disable: 'Désactiver',
      delete: 'Supprimer',
      confirmDelete: 'Supprimer l’interface "{name}" ?'
    },
    columns: {
      name: 'Nom',
      type: 'Type',
      module: 'ID du module',
      enabled: 'Activée',
      connected: 'Connectée',
      lastEvent: 'Dernier événement',
      actions: 'Actions'
    },
    state: {
      enabled: 'oui',
      disabled: 'non',
      connected: 'connectée',
      disconnected: 'déconnectée'
    },
    empty: 'Aucune interface configurée pour le moment.',
    form: {
      createTitle: 'Créer une interface',
      editTitle: 'Modifier l’interface : {name}',
      name: 'Nom de l’instance',
      type: 'Type d’interface',
      moduleId: 'ID du module',
      enabled: 'Activée',
      configJson: 'JSON de configuration',
      create: 'Créer',
      save: 'Enregistrer',
      cancel: 'Annuler',
      irc: {
        host: 'Hôte',
        port: 'Port',
        nick: 'Pseudo',
        serverPassword: 'Mot de passe du serveur',
        username: 'Nom d’utilisateur',
        realname: 'Nom réel',
        dmOnly: 'Mode MP uniquement',
        respondToChannel: 'Répondre à l’activité des canaux',
        respondToDm: 'Répondre aux messages directs',
        respondToNotices: 'Répondre aux notifications',
        allowedDmUsers: 'Utilisateurs MP autorisés',
        allowedDmUsersHint: 'Liste d’autorisation facultative ; séparée par virgules ou retours à la ligne.',
        agentId: 'ID de l’agent',
        reconnectEnabled: 'Reconnexion activée',
        reconnectInitialDelay: 'Délai initial de reconnexion (ms)',
        reconnectMaxDelay: 'Délai max. de reconnexion (ms)'
      }
    },
    createSuccess: 'Interface "{name}" créée.',
    updateSuccess: 'Interface "{name}" mise à jour.',
    deleteSuccess: 'Interface "{name}" supprimée.'
  } as const;
