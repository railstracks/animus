export const memoryFiles = {
    title: 'Arquivos de memória',
    subtitle: 'Armazene artefatos de texto bruto e pesquise em domínios de memória.',
    common: {
      yes: 'sim',
      no: 'não'
    },
    actions: {
      refresh: 'Atualizar',
      open: 'Abrir',
      delete: 'Excluir',
      process: 'Fila para processamento',
      import: 'Importar',
      importBatch: 'Importar lote',
      saveMetadata: 'Salvar metadados',
      search: 'Pesquisar'
    },
    fields: {
      sourcePath: 'Caminho de origem',
      fileType: 'Tipo de arquivo',
      contentMutable: 'Conteúdo mutável',
      agentId: 'ID do agente',
      status: 'Estado',
      agentFilter: 'Filtrar por Agente',
      allAgents: 'Todos os agentes',
      superseded: 'Substituído',
      content: 'Conteúdo'
    },
    types: {
      all: 'Todos os tipos',
      expanded_memory: 'Memória Expandida',
      session_log: 'Registro de sessão',
      daily_note: 'Nota Diária',
      bootstrap_file: 'Arquivo de inicialização',
      journal: 'Diário',
      external_doc: 'Documento externo'
    },
    stats: {
      title: 'Estatísticas'
    },
    list: {
      title: 'Lista de arquivos',
      typeFilter: 'Tipo Filtro',
      limit: 'Limite',
      offset: 'Deslocamento',
      columns: {
        id: 'ID',
        type: 'Tipo',
        sourcePath: 'Caminho de origem',
        contentMutable: 'Mutável',
        agentId: 'ID do agente',
      status: 'Estado',
      agentFilter: 'Filtrar por Agente',
      allAgents: 'Todos os agentes',
        superseded: 'Substituído',
        importedAt: 'Importado',
        actions: 'Ações'
      },
      status: {
        unprocessed: 'Não processado',
        processed: 'Processado'
      }
    },
    import: {
      singleTitle: 'Importar arquivo',
      selectFile: 'Selecione o arquivo',
      selected: 'Selecionado',
      batchTitle: 'Importação em lote',
      selectFiles: 'Selecione os arquivos',
      filesSelected: 'arquivos selecionados'
    },
    detail: {
      title: 'Detalhes do arquivo',
      empty: 'Selecione um arquivo da lista para inspecionar e editar metadados.',
      createdAt: 'Criado',
      importedAt: 'Importado',
      contentTitle: 'Conteúdo',
      contentImmutableNotice: 'A edição de conteúdo está desabilitada porque este arquivo está marcado como imutável.'
    },
    search: {
      title: 'Pesquisa Unificada',
      query: 'Consulta de pesquisa',
      limit: 'Limite',
      relevance: 'Relevância',
      empty: 'Nenhum resultado de pesquisa ainda.',
      domains: {
        observation: 'Observação',
        observations: 'Observações',
        ontology: 'Ontologia',
        memory_file: 'Arquivos',
        sessions: 'Sessões'
      }
    },
    errors: {
      loadFiles: 'Falha ao carregar arquivos de memória.',
      loadDetail: 'Falha ao carregar detalhes do arquivo.',
      importRequired: 'Selecione um arquivo para importar.',
      importSingle: 'Falha ao importar arquivo de memória.',
      batchRequired: 'Selecione pelo menos um arquivo para importação em lote.',
      importBatch: 'Falha ao importar carga em lote.',
      updateMetadata: 'Falha ao atualizar metadados.',
      delete: 'Falha ao excluir arquivo de memória.',
      search: 'Falha na pesquisa de memória.'
    }
  } as const;
