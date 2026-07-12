export const memorySearch = {
    title: 'Memory Search',
    subtitle: 'Test agent memory queries across all data domains. See exactly what an agent would find.',
    agentLabel: 'Agent',
    queryLabel: 'Search query',
    searchButton: 'Search',
    domainsLabel: 'Data sources',
    domains: {
      episodic: 'Episodic Memory',
      semantic: 'Semantic Memory',
      files: 'Memory Files',
      sessions: 'Sessions'
    },
    limitLabel: 'Result limit',
    resultsCount: 'results',
    noResults: 'No results found.',
    placeholder: 'Enter a search query to test memory retrieval.',
    searching: 'Searching...'
  } as const;
