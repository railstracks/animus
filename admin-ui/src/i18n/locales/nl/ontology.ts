export const ontology = {
    title: 'Ontologieverkenner',
    subtitle: 'Blader door semantische entiteiten en hun observatie-gekoppelde eigenschappen.',
    actions: {
      refresh: 'Vernieuwen'
    },
    sections: {
      agent: 'Agent',
      categories: 'Categorieën',
      entities: 'Entiteiten',
      details: 'Entiteitdetails',
      properties: 'Eigenschappen'
    },
    states: {
      new: 'nieuw',
      current: 'actueel',
      deprecated: 'verouderd'
    },
    empty: {
      entities: 'Geen entiteiten gevonden voor deze categorie.',
      details: 'Selecteer een entiteit om eigenschappen te bekijken.'
    }
  } as const;
