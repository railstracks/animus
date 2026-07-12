export const ontology = {
    title: 'Eksplorator ontologii',
    subtitle: 'Przeglądaj jednostki semantyczne i ich właściwości poparte obserwacjami.',
    actions: {
      refresh: 'Odśwież'
    },
    sections: {
      agent: 'Agent',
      categories: 'Kategorie',
      entities: 'Podmioty',
      details: 'Szczegóły podmiotu',
      properties: 'Właściwości'
    },
    states: {
      new: 'nowy',
      current: 'prąd',
      deprecated: 'przestarzałe'
    },
    empty: {
      entities: 'Nie znaleziono podmiotów w tej kategorii.',
      details: 'Wybierz obiekt, aby sprawdzić właściwości.'
    }
  } as const;
