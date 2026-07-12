export const memorySearch = {
    title: 'Пошук по пам\'яті',
    subtitle: 'Тестування запитів до пам’яті агента в усіх доменах даних. Подивіться, що саме знайде агент.',
    agentLabel: 'Агент',
    queryLabel: 'Пошуковий запит',
    searchButton: 'Пошук',
    domainsLabel: 'Джерела даних',
    domains: {
      episodic: 'Епізодична пам\'ять',
      semantic: 'Семантична пам\'ять',
      files: 'Файли пам\'яті',
      sessions: 'Сеанси'
    },
    limitLabel: 'Межа результату',
    resultsCount: 'результати',
    noResults: 'Результатів не знайдено.',
    placeholder: 'Введіть пошуковий запит, щоб перевірити пошук пам’яті.',
    searching: 'Пошук...'
  } as const;
