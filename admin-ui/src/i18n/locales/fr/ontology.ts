export const ontology = {
    title: 'Explorateur d\'ontologies',
    subtitle: 'Parcourez les entités sémantiques et leurs propriétés basées sur l\'observation.',
    actions: {
      refresh: 'Actualiser'
    },
    sections: {
      agent: 'Agent',
      categories: 'Catégories',
      entities: 'Entités',
      details: 'Détails de l\'entité',
      properties: 'Propriétés'
    },
    states: {
      new: 'nouveau',
      current: 'courant',
      deprecated: 'obsolète'
    },
    empty: {
      entities: 'Aucune entité trouvée pour cette catégorie.',
      details: 'Sélectionnez une entité pour inspecter les propriétés.'
    }
  } as const;
