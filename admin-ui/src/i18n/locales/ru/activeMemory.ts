export const activeMemory = {
    title: 'Активная память',
    subtitle: 'Собранный контекст агента — то, что агент видит в своей преамбуле.',
    empty: 'Выберите агент, чтобы просмотреть его собранный контекст.',
    actions: {
      refresh: 'Обновить'
    },
    labels: {
      agent: 'Агент',
      session: 'Сессия',
      syntheticSession: 'Синтетический (пустой сеанс для тестирования)',
      blocks: 'блоки'
    },
    flags: {
      synthetic: 'Синтетическая сессия',
      live: 'Живая сессия'
    },
    sections: {
      rendered: 'Обработанный вывод',
      blocks: 'Разбивка блоков'
    }
  } as const;
