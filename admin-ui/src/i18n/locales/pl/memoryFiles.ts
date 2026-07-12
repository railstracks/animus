export const memoryFiles = {
    title: 'Pliki pamięci',
    subtitle: 'Przechowuj artefakty nieprzetworzonego tekstu i przeszukuj domeny pamięci.',
    common: {
      yes: 'tak',
      no: 'nie'
    },
    actions: {
      refresh: 'Odśwież',
      open: 'Otwórz',
      delete: 'Usuń',
      process: 'Kolejka do przetworzenia',
      import: 'Importuj',
      importBatch: 'Importuj partię',
      saveMetadata: 'Zapisz metadane',
      search: 'Szukaj'
    },
    fields: {
      sourcePath: 'Ścieżka źródłowa',
      fileType: 'Typ pliku',
      contentMutable: 'Treść zmienna',
      agentId: 'Identyfikator agenta',
      status: 'Stan',
      agentFilter: 'Filtruj według agenta',
      allAgents: 'Wszyscy agenci',
      superseded: 'Zastąpione',
      content: 'Treść'
    },
    types: {
      all: 'Wszystkie typy',
      expanded_memory: 'Rozszerzona pamięć',
      session_log: 'Dziennik sesji',
      daily_note: 'Codzienna notatka',
      bootstrap_file: 'Plik Bootstrapa',
      journal: 'Dziennik',
      external_doc: 'Dokument zewnętrzny'
    },
    stats: {
      title: 'Statystyki'
    },
    list: {
      title: 'Lista plików',
      typeFilter: 'Wpisz filtr',
      limit: 'Limit',
      offset: 'Przesunięcie',
      columns: {
        id: 'Identyfikator',
        type: 'Wpisz',
        sourcePath: 'Ścieżka źródłowa',
        contentMutable: 'Zmienny',
        agentId: 'Identyfikator agenta',
      status: 'Stan',
      agentFilter: 'Filtruj według agenta',
      allAgents: 'Wszyscy agenci',
        superseded: 'Zastąpione',
        importedAt: 'Importowane',
        actions: 'Działania'
      },
      status: {
        unprocessed: 'Nieprzetworzone',
        processed: 'Przetworzone'
      }
    },
    import: {
      singleTitle: 'Importuj plik',
      selectFile: 'Wybierz plik',
      selected: 'Wybrane',
      batchTitle: 'Import wsadowy',
      selectFiles: 'Wybierz pliki',
      filesSelected: 'wybrane pliki'
    },
    detail: {
      title: 'Szczegóły pliku',
      empty: 'Wybierz plik z listy, aby sprawdzić i edytować metadane.',
      createdAt: 'Utworzono',
      importedAt: 'Importowane',
      contentTitle: 'Treść',
      contentImmutableNotice: 'Edycja treści jest wyłączona, ponieważ ten plik jest oznaczony jako niezmienny.'
    },
    search: {
      title: 'Ujednolicone wyszukiwanie',
      query: 'Zapytanie wyszukiwania',
      limit: 'Limit',
      relevance: 'Trafność',
      empty: 'Nie ma jeszcze wyników wyszukiwania.',
      domains: {
        observation: 'Obserwacja',
        observations: 'Obserwacje',
        ontology: 'Ontologia',
        memory_file: 'Pliki',
        sessions: 'Sesje'
      }
    },
    errors: {
      loadFiles: 'Nie udało się załadować plików pamięci.',
      loadDetail: 'Nie udało się załadować szczegółów pliku.',
      importRequired: 'Wybierz plik do zaimportowania.',
      importSingle: 'Nie udało się zaimportować pliku pamięci.',
      batchRequired: 'Wybierz co najmniej jeden plik do importu zbiorczego.',
      importBatch: 'Nie udało się zaimportować ładunku zbiorczego.',
      updateMetadata: 'Nie udało się zaktualizować metadanych.',
      delete: 'Nie udało się usunąć pliku pamięci.',
      search: 'Wyszukiwanie pamięci nie powiodło się.'
    }
  } as const;
