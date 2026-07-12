export const activeMemory = {
    title: 'Активна пам\'ять',
    subtitle: 'Зібраний контекст агента — те, що агент бачить у своїй преамбулі',
    empty: 'Виберіть агента, щоб переглянути його зібраний контекст.',
    actions: {
      refresh: 'Оновити'
    },
    labels: {
      agent: 'Агент',
      session: 'Сесія',
      syntheticSession: 'Синтетичний (порожня сесія для тестування)',
      blocks: 'блоки'
    },
    flags: {
      synthetic: 'Синтетична сесія',
      live: 'Жива сесія'
    },
    sections: {
      rendered: 'Відтворений вихід',
      blocks: 'Розбивка блоку'
    }
  } as const;
