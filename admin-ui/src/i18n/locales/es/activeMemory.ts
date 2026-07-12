export const activeMemory = {
    title: 'Memoria activa',
    subtitle: 'Contexto del agente ensamblado: lo que el agente ve en su preámbulo',
    empty: 'Seleccione un agente para ver su contexto ensamblado.',
    actions: {
      refresh: 'Actualizar'
    },
    labels: {
      agent: 'Agente',
      session: 'Sesión',
      syntheticSession: 'Sintético (sesión vacía para pruebas)',
      blocks: 'bloques'
    },
    flags: {
      synthetic: 'Sesión sintética',
      live: 'Sesión en vivo'
    },
    sections: {
      rendered: 'Salida renderizada',
      blocks: 'Desglose de bloques'
    }
  } as const;
