export const memoryFiles = {
  title: 'Geheugenbestanden',
  subtitle: 'Sla onbewerkte tekstartefacten op en zoek in alle geheugendomeinen.',
  common: {
    yes: 'ja',
    no: 'nee'
  },
  actions: {
    refresh: 'Vernieuwen',
    open: 'Openen',
    delete: 'Verwijderen',
    process: 'In wachtrij voor verwerking',
    import: 'Importeren',
    importBatch: 'Batch importeren',
    saveMetadata: 'Metadata opslaan',
    search: 'Zoeken'
  },
  fields: {
    sourcePath: 'Bronpad',
    fileType: 'Bestandstype',
    contentMutable: 'Inhoud wijzigbaar',
    agentId: 'Agent-ID',
    status: 'Status',
    agentFilter: 'Filter op agent',
    allAgents: 'Alle agenten',
    superseded: 'Vervangen',
    content: 'Inhoud'
  },
  types: {
    all: 'Alle typen',
    expanded_memory: 'Uitgebreid geheugen',
    session_log: 'Sessielog',
    daily_note: 'Dagnotitie',
    bootstrap_file: 'Bootstrapbestand',
    journal: 'Dagboek',
    external_doc: 'Extern document'
  },
  stats: {
    title: 'Statistieken'
  },
  list: {
    title: 'Bestandslijst',
    typeFilter: 'Typefilter',
    limit: 'Limiet',
    offset: 'Offset',
    columns: {
      id: 'ID',
      type: 'Type',
      sourcePath: 'Bronpad',
      contentMutable: 'Wijzigbaar',
      agentId: 'Agent-ID',
      status: 'Status',
      agentFilter: 'Filter op agent',
      allAgents: 'Alle agenten',
      superseded: 'Vervangen',
      importedAt: 'Geïmporteerd',
      actions: 'Acties'
    },
    status: {
      unprocessed: 'Niet verwerkt',
      processed: 'Verwerkt'
    }
  },
  import: {
    singleTitle: 'Bestand importeren',
    selectFile: 'Bestand selecteren',
    selected: 'Geselecteerd',
    batchTitle: 'Batch importeren',
    selectFiles: 'Bestanden selecteren',
    filesSelected: 'bestanden geselecteerd'
  },
  detail: {
    title: 'Bestandsdetails',
    empty: 'Selecteer een bestand uit de lijst om de metadata te bekijken en te bewerken.',
    createdAt: 'Aangemaakt',
    importedAt: 'Geïmporteerd',
    contentTitle: 'Inhoud',
    contentImmutableNotice: 'Bewerken van de inhoud is uitgeschakeld omdat dit bestand als onveranderlijk is gemarkeerd.'
  },
  search: {
    title: 'Gecombineerd zoeken',
    query: 'Zoekopdracht',
    limit: 'Limiet',
    relevance: 'Relevantie',
    empty: 'Nog geen zoekresultaten.',
    domains: {
      observation: 'Observatie',
      observations: 'Observaties',
      ontology: 'Ontologie',
      memory_file: 'Bestanden',
      sessions: 'Sessies'
    }
  },
  errors: {
    loadFiles: 'Laden van geheugenbestanden mislukt.',
    loadDetail: 'Laden van bestandsdetails mislukt.',
    importRequired: 'Selecteer een bestand om te importeren.',
    importSingle: 'Importeren van geheugenbestand mislukt.',
    batchRequired: 'Selecteer ten minste één bestand voor batchimport.',
    importBatch: 'Importeren van batchgegevens mislukt.',
    updateMetadata: 'Bijwerken van metadata mislukt.',
    delete: 'Verwijderen van geheugenbestand mislukt.',
    search: 'Zoeken in geheugen mislukt.'
  }
} as const;