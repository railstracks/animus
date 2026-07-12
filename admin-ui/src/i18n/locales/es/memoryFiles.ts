export const memoryFiles = {
    title: 'Archivos de memoria',
    subtitle: 'Almacene artefactos de texto sin formato y busque en dominios de memoria.',
    common: {
      yes: 'si',
      no: 'no'
    },
    actions: {
      refresh: 'Actualizar',
      open: 'Abierto',
      delete: 'Eliminar',
      process: 'Cola para procesamiento',
      import: 'Importar',
      importBatch: 'Importar lote',
      saveMetadata: 'Guardar metadatos',
      search: 'Buscar'
    },
    fields: {
      sourcePath: 'Ruta de origen',
      fileType: 'Tipo de archivo',
      contentMutable: 'Contenido mutable',
      agentId: 'ID del agente',
      status: 'Estado',
      agentFilter: 'Filtrar por Agente',
      allAgents: 'Todos los agentes',
      superseded: 'Reemplazado',
      content: 'Contenido'
    },
    types: {
      all: 'Todos los tipos',
      expanded_memory: 'Memoria ampliada',
      session_log: 'Registro de sesión',
      daily_note: 'Nota diaria',
      bootstrap_file: 'Archivo de arranque',
      journal: 'Diario',
      external_doc: 'Documento externo'
    },
    stats: {
      title: 'Estadísticas'
    },
    list: {
      title: 'Lista de archivos',
      typeFilter: 'Tipo de filtro',
      limit: 'Límite',
      offset: 'compensación',
      columns: {
        id: 'identificación',
        type: 'Tipo',
        sourcePath: 'Ruta de origen',
        contentMutable: 'Mutable',
        agentId: 'ID del agente',
      status: 'Estado',
      agentFilter: 'Filtrar por Agente',
      allAgents: 'Todos los agentes',
        superseded: 'Reemplazado',
        importedAt: 'Importado',
        actions: 'Acciones'
      },
      status: {
        unprocessed: 'Sin procesar',
        processed: 'Procesado'
      }
    },
    import: {
      singleTitle: 'Importar archivo',
      selectFile: 'Seleccionar archivo',
      selected: 'Seleccionado',
      batchTitle: 'Importación por lotes',
      selectFiles: 'Seleccionar archivos',
      filesSelected: 'archivos seleccionados'
    },
    detail: {
      title: 'Detalle del archivo',
      empty: 'Seleccione un archivo de la lista para inspeccionar y editar metadatos.',
      createdAt: 'Creado',
      importedAt: 'Importado',
      contentTitle: 'Contenido',
      contentImmutableNotice: 'La edición de contenido está deshabilitada porque este archivo está marcado como inmutable.'
    },
    search: {
      title: 'Búsqueda unificada',
      query: 'Consulta de búsqueda',
      limit: 'Límite',
      relevance: 'Relevancia',
      empty: 'Aún no hay resultados de búsqueda.',
      domains: {
        observation: 'Observación',
        observations: 'Observaciones',
        ontology: 'ontología',
        memory_file: 'Archivos',
        sessions: 'Sesiones'
      }
    },
    errors: {
      loadFiles: 'No se pudieron cargar los archivos de memoria.',
      loadDetail: 'No se pudieron cargar los detalles del archivo.',
      importRequired: 'Seleccione un archivo para importar.',
      importSingle: 'No se pudo importar el archivo de memoria.',
      batchRequired: 'Seleccione al menos un archivo para importar por lotes.',
      importBatch: 'No se pudo importar la carga útil por lotes.',
      updateMetadata: 'No se pudieron actualizar los metadatos.',
      delete: 'No se pudo eliminar el archivo de memoria.',
      search: 'La búsqueda de memoria falló.'
    }
  } as const;
