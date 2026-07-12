export const memoryFiles = {
    title: 'Memory Files',
    subtitle: 'Store raw text artifacts and search across memory domains.',
    common: {
      yes: 'yes',
      no: 'no'
    },
    actions: {
      refresh: 'Refresh',
      open: 'Open',
      delete: 'Delete',
      process: 'Queue for Processing',
      import: 'Import',
      importBatch: 'Import Batch',
      saveMetadata: 'Save Metadata',
      search: 'Search'
    },
    fields: {
      sourcePath: 'Source Path',
      fileType: 'File Type',
      contentMutable: 'Content Mutable',
      agentId: 'Agent ID',
      status: 'Status',
      agentFilter: 'Filter by Agent',
      allAgents: 'All Agents',
      superseded: 'Superseded',
      content: 'Content'
    },
    types: {
      all: 'All Types',
      expanded_memory: 'Expanded Memory',
      session_log: 'Session Log',
      daily_note: 'Daily Note',
      bootstrap_file: 'Bootstrap File',
      journal: 'Journal',
      external_doc: 'External Doc'
    },
    stats: {
      title: 'Stats'
    },
    list: {
      title: 'File List',
      typeFilter: 'Type Filter',
      limit: 'Limit',
      offset: 'Offset',
      columns: {
        id: 'ID',
        type: 'Type',
        sourcePath: 'Source Path',
        contentMutable: 'Mutable',
        agentId: 'Agent ID',
      status: 'Status',
      agentFilter: 'Filter by Agent',
      allAgents: 'All Agents',
        superseded: 'Superseded',
        importedAt: 'Imported',
        actions: 'Actions'
      },
      status: {
        unprocessed: 'Unprocessed',
        processed: 'Processed'
      }
    },
    import: {
      singleTitle: 'Import File',
      selectFile: 'Select file',
      selected: 'Selected',
      batchTitle: 'Batch Import',
      selectFiles: 'Select files',
      filesSelected: 'files selected'
    },
    detail: {
      title: 'File Detail',
      empty: 'Select a file from the list to inspect and edit metadata.',
      createdAt: 'Created',
      importedAt: 'Imported',
      contentTitle: 'Content',
      contentImmutableNotice: 'Content editing is disabled because this file is marked immutable.'
    },
    search: {
      title: 'Unified Search',
      query: 'Search Query',
      limit: 'Limit',
      relevance: 'Relevance',
      empty: 'No search results yet.',
      domains: {
        observation: 'Observation',
        observations: 'Observations',
        ontology: 'Ontology',
        memory_file: 'Files',
        sessions: 'Sessions'
      }
    },
    errors: {
      loadFiles: 'Failed to load memory files.',
      loadDetail: 'Failed to load file detail.',
      importRequired: 'Select a file to import.',
      importSingle: 'Failed to import memory file.',
      batchRequired: 'Select at least one file for batch import.',
      importBatch: 'Failed to import batch payload.',
      updateMetadata: 'Failed to update metadata.',
      delete: 'Failed to delete memory file.',
      search: 'Memory search failed.'
    }
  } as const;
