export const activeMemory = {
    title: 'Aktywna pamięć',
    subtitle: 'Złożony kontekst agenta — co agent widzi w swojej preambule',
    empty: 'Wybierz agenta, aby wyświetlić jego złożony kontekst.',
    actions: {
      refresh: 'Odśwież'
    },
    labels: {
      agent: 'Agent',
      session: 'Sesja',
      syntheticSession: 'Syntetyczny (pusta sesja do testowania)',
      blocks: 'bloki'
    },
    flags: {
      synthetic: 'Sesja syntetyczna',
      live: 'Sesja na żywo'
    },
    sections: {
      rendered: 'Renderowane dane wyjściowe',
      blocks: 'Podział bloku'
    }
  } as const;
