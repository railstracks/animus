export const memoryFiles = {
    title: 'Speicherdateien',
    subtitle: 'Speichern Sie Rohtextartefakte und durchsuchen Sie Speicherdomänen.',
    common: {
      yes: 'ja',
      no: 'Nein'
    },
    actions: {
      refresh: 'Aktualisieren',
      open: 'Offen',
      delete: 'Löschen',
      process: 'Warteschlange zur Bearbeitung',
      import: 'Importieren',
      importBatch: 'Stapel importieren',
      saveMetadata: 'Metadaten speichern',
      search: 'Suchen'
    },
    fields: {
      sourcePath: 'Quellpfad',
      fileType: 'Dateityp',
      contentMutable: 'Inhalt veränderbar',
      agentId: 'Agenten-ID',
      status: 'Status',
      agentFilter: 'Nach Agent filtern',
      allAgents: 'Alle Agenten',
      superseded: 'Abgelöst',
      content: 'Inhalt'
    },
    types: {
      all: 'Alle Typen',
      expanded_memory: 'Erweiterter Speicher',
      session_log: 'Sitzungsprotokoll',
      daily_note: 'Tägliche Notiz',
      bootstrap_file: 'Bootstrap-Datei',
      journal: 'Tagebuch',
      external_doc: 'Externer Dok'
    },
    stats: {
      title: 'Statistiken'
    },
    list: {
      title: 'Dateiliste',
      typeFilter: 'Geben Sie Filter ein',
      limit: 'Begrenzen',
      offset: 'Versatz',
      columns: {
        id: 'Ausweis',
        type: 'Typ',
        sourcePath: 'Quellpfad',
        contentMutable: 'Veränderlich',
        agentId: 'Agenten-ID',
      status: 'Status',
      agentFilter: 'Nach Agent filtern',
      allAgents: 'Alle Agenten',
        superseded: 'Abgelöst',
        importedAt: 'Importiert',
        actions: 'Aktionen'
      },
      status: {
        unprocessed: 'Unbearbeitet',
        processed: 'Verarbeitet'
      }
    },
    import: {
      singleTitle: 'Datei importieren',
      selectFile: 'Datei auswählen',
      selected: 'Ausgewählt',
      batchTitle: 'Batch-Import',
      selectFiles: 'Wählen Sie Dateien aus',
      filesSelected: 'Dateien ausgewählt'
    },
    detail: {
      title: 'Dateidetails',
      empty: 'Wählen Sie eine Datei aus der Liste aus, um Metadaten zu überprüfen und zu bearbeiten.',
      createdAt: 'Erstellt',
      importedAt: 'Importiert',
      contentTitle: 'Inhalt',
      contentImmutableNotice: 'Die Inhaltsbearbeitung ist deaktiviert, da diese Datei als unveränderlich markiert ist.'
    },
    search: {
      title: 'Einheitliche Suche',
      query: 'Suchanfrage',
      limit: 'Begrenzen',
      relevance: 'Relevanz',
      empty: 'Noch keine Suchergebnisse.',
      domains: {
        observation: 'Beobachtung',
        observations: 'Beobachtungen',
        ontology: 'Ontologie',
        memory_file: 'Dateien',
        sessions: 'Sitzungen'
      }
    },
    errors: {
      loadFiles: 'Speicherdateien konnten nicht geladen werden.',
      loadDetail: 'Dateidetails konnten nicht geladen werden.',
      importRequired: 'Wählen Sie eine Datei zum Importieren aus.',
      importSingle: 'Fehler beim Importieren der Speicherdatei.',
      batchRequired: 'Wählen Sie mindestens eine Datei für den Batch-Import aus.',
      importBatch: 'Batch-Nutzlast konnte nicht importiert werden.',
      updateMetadata: 'Metadaten konnten nicht aktualisiert werden.',
      delete: 'Speicherdatei konnte nicht gelöscht werden.',
      search: 'Die Speichersuche ist fehlgeschlagen.'
    }
  } as const;
