export const ontology = {
    title: 'Ontology Explorer',
    subtitle: 'Browse semantic entities and their observation-backed properties.',
    actions: {
      refresh: 'Refresh'
    },
    sections: {
      agent: 'Agent',
      categories: 'Categories',
      entities: 'Entities',
      details: 'Entity Details',
      properties: 'Properties'
    },
    states: {
      new: 'new',
      current: 'current',
      deprecated: 'deprecated'
    },
    empty: {
      entities: 'No entities found for this category.',
      details: 'Select an entity to inspect properties.'
    }
  } as const;
