export const memorySearch = {
    title: 'Wyszukiwanie pamięci',
    subtitle: 'Testuj zapytania dotyczące pamięci agenta we wszystkich domenach danych. Zobacz dokładnie, co znalazłby agent.',
    agentLabel: 'Agent',
    queryLabel: 'Zapytanie wyszukiwania',
    searchButton: 'Szukaj',
    domainsLabel: 'Źródła danych',
    domains: {
      episodic: 'Pamięć epizodyczna',
      semantic: 'Pamięć semantyczna',
      files: 'Pliki pamięci',
      sessions: 'Sesje'
    },
    limitLabel: 'Limit wyników',
    resultsCount: 'wyniki',
    noResults: 'Nie znaleziono żadnych wyników.',
    placeholder: 'Wprowadź zapytanie wyszukiwania, aby przetestować pobieranie pamięci.',
    searching: 'Wyszukiwanie...'
  } as const;
