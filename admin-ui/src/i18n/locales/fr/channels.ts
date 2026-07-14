export const channels = {
    title: 'Canaux',
    empty: 'Aucun canal configuré. Ajoutez-en un pour commencer.',
    createSuccess: 'La chaîne "{name}" a été créée avec succès.',
    updateSuccess: 'La chaîne "{name}" a été mise à jour avec succès.',
    deleteSuccess: 'Chaîne "{name}" supprimée.',
    columns: {
      name: 'Nom',
      type: 'Tapez',
      identity: 'Identité',
      endpoint: 'Point de terminaison',
      enabled: 'Activé',
      connected: 'Connecté',
      lastEvent: 'Dernier événement',
      actions: 'Actions'
    },
    state: {
      connected: 'Connecté',
      disconnected: 'Déconnecté'
    },
    actions: {
      refresh: 'Actualiser',
      add: 'Ajouter une chaîne',
      cancel: 'Annuler',
      create: 'Créer',
      save: 'Enregistrer',
      confirmDelete: 'Supprimer la chaîne "{name}" ? Cela ne peut pas être annulé.'
    },
    form: {
      createTitle: 'Ajouter une chaîne',
      editTitle: 'Modifier "{name}"',
      name: 'Nom de la chaîne',
      type: 'Type de canal',
      agent: 'Agent',
      minResponseInterval: 'Intervalle de réponse minimum (secondes)',
      allowInterjection: "Autoriser l'interjection",
      irc: {
        host: 'Serveur IRC',
        port: 'Port',
        serverPassword: 'Mot de passe du serveur',
        nick: 'Surnom',
        username: 'Nom d\'utilisateur',
        realname: 'Vrai nom',
        channels: 'Canaux',
        channelsHint: 'Un par ligne. Format : #canal [clé]',
        agent: 'Agent',
        respondToChannel: 'Répondre aux messages du canal',
        respondToDm: 'Répondre aux messages directs',
        respondToNotices: 'Répondre aux notifications',
        allowedDmUsers: 'Utilisateurs DM autorisés',
        reconnect: 'Reconnexion automatique'
      },
      telegram: {
        botToken: 'Jeton de robot',
        tokenHint: 'Laisser vide pour conserver le jeton existant'
      },
      vk: {
        accessToken: 'Jeton d\'accès à la communauté',
        groupId: 'Identifiant du groupe'
      },
      bluesky: {
        handle: 'Poignée',
        appPassword: 'Mot de passe de l\'application',
        pds: 'URL du PDS'
      },
      mastodon: {
        handle: 'Poignée',
        instance: 'URL de l\'instance'
      },
      email: {
        apiKey: 'Clé API AgentMail',
        apiKeyHint: 'Laisser vide pour conserver la clé actuelle',
        inboxId: 'Identifiant de la boîte de réception',
        pollingWait: 'Intervalle d\'interrogation (secondes)'
      },
      twitter: {
        tier: 'Niveau API',
        clientId: 'ID client OAuth',
        clientSecret: 'Secret client OAuth',
        accessToken: 'Jeton d\'accès',
        tokenHint: 'Laisser vide pour conserver le jeton existant',
        refreshToken: 'Actualiser le jeton',
        authorize: 'Autoriser avec Twitter',
        oauthStarted: 'Fenêtre d\'autorisation ouverte. Complétez le flux dans votre navigateur.'
      },
      discord: {
        botToken: 'Jeton de robot',
        tokenHint: 'Laisser vide pour conserver le jeton existant',
        applicationId: 'ID d\'application (pour les commandes slash)',
        monitoredChannels: 'Canaux surveillés',
        monitoredChannelsHint: 'Un identifiant de canal par ligne. Le bot doit être sur le serveur.',
        respondToDm: 'Répondre aux DM',
        respondToMentions: 'Répondre aux mentions'
      },
      slack: {
        botToken: 'Jeton de robot (xoxb-)',
        tokenHint: 'Laisser vide pour conserver le jeton existant',
        appToken: 'Jeton d\'application (xapp-)',
        appTokenHint: 'Pour le mode prise (phase 2). Facultatif pour la phase 1.',
        monitoredChannels: 'Canaux surveillés (remplacement)',
        monitoredChannelsHint: 'Le robot surveille automatiquement tous les canaux dont il est membre. Ajoutez uniquement des identifiants ici pour limiter la portée.',
        respondToMentions: "Répondre aux mentions {'@'}",
        respondToAll: 'Répondre à tous les messages (ignore le filtre de mention)',
        threadedReplies: 'Réponses au fil de discussion dans les canaux (chaque message démarre un fil de discussion)'
      }
    }
  } as const;
