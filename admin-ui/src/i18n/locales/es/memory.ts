export const memory = {
    title: 'Explorador de memoria',
    actions: {
      refresh: 'Actualizar',
      triggerConsolidation: 'Ejecutar consolidación'
    },
    common: {
      notAvailable: 'n/a'
    },
    layers: {
      title: 'capas',
      empty: 'No se encontraron capas de memoria.',
      horizon: 'Horizonte',
      consolidationInterval: 'Intervalo de consolidación',
      retentionPolicy: 'Política de retención',
      perspectiveCurrent: 'Perspectiva actual',
      perspectivePast: 'Perspectiva pasada',
      perspectiveFuture: 'Perspectiva de futuro',
      viewObservations: 'Ver observaciones'
    },
    consolidation: {
      title: 'Consolidación',
      activeJob: 'Trabajo activo',
      lastRun: 'Última ejecución',
      lastJob: 'último trabajo',
      promoted: 'Promovido',
      demoted: 'Degradado',
      archived: 'Archivado',
      state: {
        running: 'corriendo',
        idle: 'inactivo'
      }
    },
    list: {
      title: 'Observaciones',
      activeLayer: 'Capa: {layer}',
      sortBy: 'ordenar',
      orderBy: 'Orden',
      tagFilter: 'Filtrar por etiquetas',
      pageSize: 'Tamaño de página',
      compactMode: 'Compacto',
      openDetail: 'Detalles',
      emptyLayer: 'Aún no hay observaciones en esta capa.',
      emptyFilter: 'Ninguna observación coincide con el filtro de etiquetas actual.',
      sort: {
        weight: 'Peso',
        age: 'edad',
        tags: 'Etiquetas'
      },
      order: {
        desc: 'Descendente',
        asc: 'Ascendente'
      },
      columns: {
        content: 'Contenido',
        tags: 'Etiquetas',
        weight: 'Peso',
        age: 'edad',
        decay: 'decadencia',
        actions: 'Acciones'
      }
    },
    detail: {
      title: 'Detalle de observación',
      empty: 'Seleccione una observación para inspeccionar y editar.',
      id: 'identificación',
      content: 'Contenido',
      layer: 'capa',

      timestamp: 'Marca de tiempo',
      demotionReason: 'Razón de degradación',
      demotionTimestamp: 'Marca de tiempo de degradación',
      editWeight: 'Peso',
      editTags: 'Etiquetas',
      save: 'Guardar',
      reset: 'Reiniciar',
      archive: 'Archivo',
      saveSuccess: 'Observación actualizada.',
      archiveSuccess: 'Observación archivada.'
    }
  } as const;
