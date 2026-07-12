export const ontology = {
    title: 'Explorador de ontologías',
    subtitle: 'Explore entidades semánticas y sus propiedades respaldadas por observaciones.',
    actions: {
      refresh: 'Actualizar'
    },
    sections: {
      agent: 'Agente',
      categories: 'Categorías',
      entities: 'Entidades',
      details: 'Detalles de la entidad',
      properties: 'Propiedades'
    },
    states: {
      new: 'nuevo',
      current: 'actual',
      deprecated: 'obsoleto'
    },
    empty: {
      entities: 'No se encontraron entidades para esta categoría.',
      details: 'Seleccione una entidad para inspeccionar propiedades.'
    }
  } as const;
