export const ontology = {
    title: 'Explorador de ontologias',
    subtitle: 'Navegue por entidades semânticas e suas propriedades baseadas em observação.',
    actions: {
      refresh: 'Atualizar'
    },
    sections: {
      agent: 'Agente',
      categories: 'Categorias',
      entities: 'Entidades',
      details: 'Detalhes da entidade',
      properties: 'Propriedades'
    },
    states: {
      new: 'novo',
      current: 'atual',
      deprecated: 'obsoleto'
    },
    empty: {
      entities: 'Nenhuma entidade encontrada para esta categoria.',
      details: 'Selecione uma entidade para inspecionar propriedades.'
    }
  } as const;
