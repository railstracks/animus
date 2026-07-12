export const memory = {
    title: 'Navigateur de mémoire',
    actions: {
      refresh: 'Actualiser',
      triggerConsolidation: 'Exécuter la consolidation'
    },
    common: {
      notAvailable: 'n/a'
    },
    layers: {
      title: 'Calques',
      empty: 'Aucune couche de mémoire trouvée.',
      horizon: 'Horizon',
      consolidationInterval: 'Intervalle de consolidation',
      retentionPolicy: 'Politique de conservation',
      perspectiveCurrent: 'Perspective actuelle',
      perspectivePast: 'Perspective passée',
      perspectiveFuture: 'Perspectives d\'avenir',
      viewObservations: 'Afficher les observations'
    },
    consolidation: {
      title: 'Consolidation',
      activeJob: 'Emploi actif',
      lastRun: 'Dernière exécution',
      lastJob: 'Dernier travail',
      promoted: 'Promu',
      demoted: 'Rétrogradé',
      archived: 'Archivé',
      state: {
        running: 'Courir',
        idle: 'Inactif'
      }
    },
    list: {
      title: 'Observations',
      activeLayer: 'Couche : {layer}',
      sortBy: 'Trier',
      orderBy: 'Commande',
      tagFilter: 'Filtrer par balises',
      pageSize: 'Taille des pages',
      compactMode: 'Compacte',
      openDetail: 'Détails',
      emptyLayer: 'Aucune observation dans cette couche pour l\'instant.',
      emptyFilter: 'Aucune observation ne correspond au filtre de balises actuel.',
      sort: {
        weight: 'Poids',
        age: 'Âge',
        tags: 'Balises'
      },
      order: {
        desc: 'Décroissant',
        asc: 'Ascendant'
      },
      columns: {
        content: 'Contenu',
        tags: 'Balises',
        weight: 'Poids',
        age: 'Âge',
        decay: 'Pourriture',
        actions: 'Actions'
      }
    },
    detail: {
      title: 'Détail de l\'observation',
      empty: 'Sélectionnez une observation à inspecter et à modifier.',
      id: 'pièce d\'identité',
      content: 'Contenu',
      layer: 'Couche',

      timestamp: 'Horodatage',
      demotionReason: 'Raison de la rétrogradation',
      demotionTimestamp: 'Horodatage de rétrogradation',
      editWeight: 'Poids',
      editTags: 'Balises',
      save: 'Enregistrer',
      reset: 'Réinitialiser',
      archive: 'Archiver',
      saveSuccess: 'Observation mise à jour.',
      archiveSuccess: 'Observation archivée.'
    }
  } as const;
