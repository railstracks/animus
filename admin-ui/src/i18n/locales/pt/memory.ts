export const memory = {
    title: 'Navegador de memória',
    actions: {
      refresh: 'Atualizar',
      triggerConsolidation: 'Executar consolidação'
    },
    common: {
      notAvailable: 'n/a'
    },
    layers: {
      title: 'Camadas',
      empty: 'Nenhuma camada de memória encontrada.',
      horizon: 'Horizonte',
      consolidationInterval: 'Intervalo de Consolidação',
      retentionPolicy: 'Política de retenção',
      perspectiveCurrent: 'Perspectiva Atual',
      perspectivePast: 'Perspectiva Passada',
      perspectiveFuture: 'Perspectiva Futura',
      viewObservations: 'Ver observações'
    },
    consolidation: {
      title: 'Consolidação',
      activeJob: 'Trabalho ativo',
      lastRun: 'Última execução',
      lastJob: 'Último Trabalho',
      promoted: 'Promovido',
      demoted: 'Rebaixado',
      archived: 'Arquivado',
      state: {
        running: 'Correndo',
        idle: 'Inativo'
      }
    },
    list: {
      title: 'Observações',
      activeLayer: 'Camada: {layer}',
      sortBy: 'Classificar',
      orderBy: 'Pedido',
      tagFilter: 'Filtrar por tags',
      pageSize: 'Tamanho da página',
      compactMode: 'Compacto',
      openDetail: 'Detalhes',
      emptyLayer: 'Nenhuma observação nesta camada ainda.',
      emptyFilter: 'Nenhuma observação corresponde ao filtro de tags atual.',
      sort: {
        weight: 'Peso',
        age: 'Idade',
        tags: 'Etiquetas'
      },
      order: {
        desc: 'Descendente',
        asc: 'Ascendente'
      },
      columns: {
        content: 'Conteúdo',
        tags: 'Etiquetas',
        weight: 'Peso',
        age: 'Idade',
        decay: 'Decadência',
        actions: 'Ações'
      }
    },
    detail: {
      title: 'Detalhe de observação',
      empty: 'Selecione uma observação para inspecionar e editar.',
      id: 'ID',
      content: 'Conteúdo',
      layer: 'Camada',

      timestamp: 'Carimbo de data e hora',
      demotionReason: 'Motivo do rebaixamento',
      demotionTimestamp: 'Carimbo de data e hora de rebaixamento',
      editWeight: 'Peso',
      editTags: 'Etiquetas',
      save: 'Salvar',
      reset: 'Redefinir',
      archive: 'Arquivo',
      saveSuccess: 'Observação atualizada.',
      archiveSuccess: 'Observação arquivada.'
    }
  } as const;
