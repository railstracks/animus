export const memoryFiles = {
    title: 'Fichiers mémoire',
    subtitle: 'Stockez des artefacts de texte brut et effectuez des recherches dans les domaines de mémoire.',
    common: {
      yes: 'oui',
      no: 'non'
    },
    actions: {
      refresh: 'Actualiser',
      open: 'Ouvert',
      delete: 'Supprimer',
      process: 'File d\'attente pour le traitement',
      import: 'Importer',
      importBatch: 'Importer un lot',
      saveMetadata: 'Enregistrer les métadonnées',
      search: 'Rechercher'
    },
    fields: {
      sourcePath: 'Chemin source',
      fileType: 'Type de fichier',
      contentMutable: 'Contenu modifiable',
      agentId: 'ID d\'agent',
      status: 'Statut',
      agentFilter: 'Filtrer par agent',
      allAgents: 'Tous les agents',
      superseded: 'Remplacé',
      content: 'Contenu'
    },
    types: {
      all: 'Tous types',
      expanded_memory: 'Mémoire étendue',
      session_log: 'Journal de session',
      daily_note: 'Note quotidienne',
      bootstrap_file: 'Fichier d\'amorçage',
      journal: 'Revue',
      external_doc: 'Document externe'
    },
    stats: {
      title: 'Statistiques'
    },
    list: {
      title: 'Liste des fichiers',
      typeFilter: 'Type de filtre',
      limit: 'Limite',
      offset: 'Décalage',
      columns: {
        id: 'pièce d\'identité',
        type: 'Tapez',
        sourcePath: 'Chemin source',
        contentMutable: 'Mutable',
        agentId: 'ID d\'agent',
      status: 'Statut',
      agentFilter: 'Filtrer par agent',
      allAgents: 'Tous les agents',
        superseded: 'Remplacé',
        importedAt: 'Importé',
        actions: 'Actions'
      },
      status: {
        unprocessed: 'Non traité',
        processed: 'Traité'
      }
    },
    import: {
      singleTitle: 'Importer un fichier',
      selectFile: 'Sélectionner un fichier',
      selected: 'Sélectionné',
      batchTitle: 'Importation par lots',
      selectFiles: 'Sélectionner des fichiers',
      filesSelected: 'fichiers sélectionnés'
    },
    detail: {
      title: 'Détails du fichier',
      empty: 'Sélectionnez un fichier dans la liste pour inspecter et modifier les métadonnées.',
      createdAt: 'Créé',
      importedAt: 'Importé',
      contentTitle: 'Contenu',
      contentImmutableNotice: 'L\'édition du contenu est désactivée car ce fichier est marqué comme immuable.'
    },
    search: {
      title: 'Recherche unifiée',
      query: 'Requête de recherche',
      limit: 'Limite',
      relevance: 'Pertinence',
      empty: 'Aucun résultat de recherche pour l\'instant.',
      domains: {
        observation: 'Observations',
        observations: 'Observations',
        ontology: 'Ontologie',
        memory_file: 'Fichiers',
        sessions: 'Séances'
      }
    },
    errors: {
      loadFiles: 'Échec du chargement des fichiers mémoire.',
      loadDetail: 'Échec du chargement des détails du fichier.',
      importRequired: 'Sélectionnez un fichier à importer.',
      importSingle: 'Échec de l\'importation du fichier mémoire.',
      batchRequired: 'Sélectionnez au moins un fichier pour l\'importation par lots.',
      importBatch: 'Échec de l\'importation de la charge utile par lots.',
      updateMetadata: 'Échec de la mise à jour des métadonnées.',
      delete: 'Échec de la suppression du fichier mémoire.',
      search: 'La recherche de mémoire a échoué.'
    }
  } as const;
