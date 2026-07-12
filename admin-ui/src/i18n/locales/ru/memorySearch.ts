export const memorySearch = {
    title: 'Поиск в памяти',
    subtitle: 'Запросы памяти агента тестирования во всех доменах данных. Посмотрите, что именно найдет агент.',
    agentLabel: 'Агент',
    queryLabel: 'Поисковый запрос',
    searchButton: 'Поиск',
    domainsLabel: 'Источники данных',
    domains: {
      episodic: 'Эпизодическая память',
      semantic: 'Семантическая память',
      files: 'Файлы памяти',
      sessions: 'Сессии'
    },
    limitLabel: 'Ограничение результатов',
    resultsCount: 'результаты',
    noResults: 'Результаты не найдены.',
    placeholder: 'Введите поисковый запрос, чтобы проверить извлечение памяти.',
    searching: 'Поиск...'
  } as const;
