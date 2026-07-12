export const providers = {
    title: 'Configuration du fournisseur',
    actions: {
      refresh: 'Actualiser',
      add: 'Ajouter un fournisseur',
      test: 'Tester',
      edit: 'Modifier',
      delete: 'Supprimer',
      setDefault: 'Définir par défaut',
      confirmDelete: 'Supprimer le fournisseur "{id}" ? Cela ne peut pas être annulé.'
    },
    columns: {
      status: 'Statut',
      provider: 'Nom',
      type: 'Tapez',
      baseUrl: 'URL de base',
      defaultModel: 'Modèle',
      default: 'Par défaut',
      actions: 'Actions'
    },
    empty: {
      title: 'Aucun fournisseur',
      description: 'Ajoutez un fournisseur LLM pour commencer.'
    },
    form: {
      createTitle: 'Ajouter un fournisseur',
      editTitle: 'Modifier le fournisseur',
      providerType: 'Type de fournisseur',
      instanceName: 'Nom de l\'instance',
      instanceNameHint: 'Un nom unique pour cette configuration de fournisseur.',
      baseUrl: 'URL de base',
      defaultModel: 'Modèle par défaut',
      defaultContextWindow: 'Fenêtre contextuelle par défaut',
      modelContextWindows: 'Remplacements de contexte par modèle',
      modelContextWindowsHint: 'Objet JSON, par ex. gpt-5.2 : 200 000',
      modelContextWindowsInvalid: 'Les remplacements de contexte par modèle doivent être du JSON valide.',
      authType: 'Type d\'authentification',
      apiKey: 'Clé API',
      apiKeyPlaceholder: 'Laisser vide pour conserver la clé actuelle',
      authFile: 'Fichier d\'authentification',
      oauthBrowserTitle: 'Connexion au navigateur (code d\'autorisation)',
      oauthRedirectUri: 'URI de redirection',
      oauthStartBrowser: 'Démarrer la connexion au navigateur',
      oauthAuthorizationUrl: 'URL d\'autorisation',
      oauthState: 'État',
      oauthCallbackUrl: 'Coller l\'URL de rappel',
      oauthCallbackHint: 'Collez l\'URL de rappel complète contenant le code et l\'état.',
      oauthCompleteBrowser: 'Connexion complète au navigateur',
      concurrency: 'Concurrence maximale',
      create: 'Créer',
      save: 'Enregistrer',
      cancel: 'Annuler',
      modelManualHint: 'Entrez l\'ID du modèle manuellement. La liste des modèles n\'est pas disponible pour ce type de fournisseur.',
      modelsLockedHint: 'Enregistrez le fournisseur avec une clé API pour déverrouiller la sélection du modèle.',
      savedContinue: 'Fournisseur enregistré. Vous pouvez maintenant sélectionner un modèle.',
      saveAndContinue: 'Enregistrer et continuer'
    },
    testSuccess: 'Le fournisseur « {id} » est joignable.',
    testFailed: 'Le test du fournisseur "{id}" a échoué',
    createSuccess: 'Fournisseur "{id}" créé.',
    updateSuccess: 'Fournisseur « {id} » mis à jour.',
    deleteSuccess: 'Fournisseur "{id}" supprimé.',
    defaultSuccess: 'Fournisseur par défaut défini sur « {id} ».',
    errors: {
      loadFailed: 'Échec du chargement des fournisseurs'
    }
  } as const;
