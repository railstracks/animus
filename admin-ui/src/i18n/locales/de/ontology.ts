export const ontology = {
    title: 'Ontologie-Explorer',
    subtitle: 'Durchsuchen Sie semantische Entitäten und ihre beobachtungsgestützten Eigenschaften.',
    actions: {
      refresh: 'Aktualisieren'
    },
    sections: {
      agent: 'Agent',
      categories: 'Kategorien',
      entities: 'Entitäten',
      details: 'Entitätsdetails',
      properties: 'Eigenschaften'
    },
    states: {
      new: 'neu',
      current: 'aktuell',
      deprecated: 'veraltet'
    },
    empty: {
      entities: 'Für diese Kategorie wurden keine Entitäten gefunden.',
      details: 'Wählen Sie eine Entität aus, um die Eigenschaften zu überprüfen.'
    }
  } as const;
