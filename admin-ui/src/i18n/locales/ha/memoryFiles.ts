export const memoryFiles = {
    title: 'Fayilolin ƙwaƙwalwa',
    subtitle: 'Ajiye ɗanyen kayan aikin rubutu kuma bincika cikin wuraren ƙwaƙwalwar ajiya.',
    common: {
      yes: 'iya',
      no: 'a\'a'
    },
    actions: {
      refresh: 'Sake sabuntawa',
      open: 'Bude',
      delete: 'Share',
      process: 'layi don sarrafawa',
      import: 'Shigo da',
      importBatch: 'Shigo da Batch',
      saveMetadata: 'Ajiye metadata',
      search: 'Bincika'
    },
    fields: {
      sourcePath: 'Hanyar Tushen',
      fileType: 'Nau\'in Fayil',
      contentMutable: 'Maɓallin abun ciki',
      agentId: 'ID na wakili',
      status: 'Matsayi',
      agentFilter: 'Tace da Wakili',
      allAgents: 'Duk Agents',
      superseded: 'Nasara',
      content: 'Abun ciki'
    },
    types: {
      all: 'Duk Iri',
      expanded_memory: 'Ƙwaƙwalwar Ƙwaƙwalwa',
      session_log: 'Log ɗin Zama',
      daily_note: 'Bayanin yau da kullun',
      bootstrap_file: 'Fayil ɗin Bootstrap',
      journal: 'Jarida',
      external_doc: 'Doc na waje'
    },
    stats: {
      title: 'Ƙididdiga'
    },
    list: {
      title: 'Jerin Fayil',
      typeFilter: 'Nau\'in Tace',
      limit: 'Iyaka',
      offset: 'Kashewa',
      columns: {
        id: 'ID',
        type: 'Nau\'in',
        sourcePath: 'Hanyar Tushen',
        contentMutable: 'Mai canzawa',
        agentId: 'ID na wakili',
      status: 'Matsayi',
      agentFilter: 'Tace da Wakili',
      allAgents: 'Duk Agents',
        superseded: 'Nasara',
        importedAt: 'Shigo da shi',
        actions: 'Ayyuka'
      },
      status: {
        unprocessed: 'Ba a sarrafa shi ba',
        processed: 'An sarrafa'
      }
    },
    import: {
      singleTitle: 'Shigo fayil',
      selectFile: 'Zaɓi fayil',
      selected: 'zaba',
      batchTitle: 'Shigo da tsari',
      selectFiles: 'Zaɓi fayiloli',
      filesSelected: 'fayilolin da aka zaɓa'
    },
    detail: {
      title: 'Cikakken Bayani',
      empty: 'Zaɓi fayil daga lissafin don dubawa da shirya metadata.',
      createdAt: 'Ƙirƙiri',
      importedAt: 'Shigo da shi',
      contentTitle: 'Abun ciki',
      contentImmutableNotice: 'An kashe gyaran abun ciki saboda an yiwa wannan fayil alamar mara canzawa.'
    },
    search: {
      title: 'Binciken Haɗin Kai',
      query: 'Neman Tambaya',
      limit: 'Iyaka',
      relevance: 'Dace',
      empty: 'Har yanzu babu sakamakon bincike.',
      domains: {
        observation: 'Lura',
        observations: 'Abun lura',
        ontology: 'Ontology',
        memory_file: 'Fayiloli',
        sessions: 'Zama'
      }
    },
    errors: {
      loadFiles: 'An kasa loda fayilolin ƙwaƙwalwar ajiya.',
      loadDetail: 'Ba a yi nasarar loda cikakken fayil ba.',
      importRequired: 'Zaɓi fayil don shigo da shi.',
      importSingle: 'An kasa shigo da fayil ɗin ƙwaƙwalwar ajiya.',
      batchRequired: 'Zaɓi aƙalla fayil ɗaya don shigo da tsari.',
      importBatch: 'Ba a yi nasarar shigo da kaya ba.',
      updateMetadata: 'An kasa sabunta metadata.',
      delete: 'An kasa share fayil ɗin ƙwaƙwalwar ajiya.',
      search: 'Binciken ƙwaƙwalwar ajiya ya kasa.'
    }
  } as const;
