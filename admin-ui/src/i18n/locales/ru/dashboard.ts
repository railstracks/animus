export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'Добро пожаловать в Анимус.',
    welcomeSub: 'Чтобы начать работу, настройте своего первого агента.',
    beginSetup: 'Начать настройку →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'Время работы',
    agents: 'Агенты',
    sessions: 'Сессии',
    providers: 'Провайдеры',
    errors: 'ошибаться',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'Недавние сессии',
    viewAll: 'Посмотреть все',
    empty: 'Сеансов пока нет.',
    startChat: 'Начать чат →',
    messages: 'сообщения',
  },
  // Memory card
  memory: {
    title: 'Память',
    inspect: 'Осмотреть',
    empty: 'Уровни памяти не настроены.',
    totalObservations: 'общее количество наблюдений',
  },
  // Provider card
  providers: {
    title: 'Провайдеры',
    manage: 'Управление',
    empty: 'Поставщики не настроены.',
    runWizard: 'Запустить мастер настройки →',
  },
  // Quick Actions card
  quickActions: {
    title: 'Быстрые действия',
    newChat: 'Новый чат',
    addAgent: 'Добавить агента',
    provider: 'Поставщик',
    logs: 'Журналы',
    scheduler: 'Планировщик',
    memory: 'Память',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'Информация о демоне',
    status: 'Статус',
    uptime: 'Время работы',
    agents: 'Агенты',
    activeSessions: 'Активные сессии',
    providers: 'Провайдеры',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'Слои памяти',
    empty: 'Уровни памяти не настроены.',
  },
  // State labels
  state: {
    loading: 'загрузка',
    unknown: 'неизвестно',
  },
  // Error messages
  errors: {
    sessions: 'Не удалось загрузить сеансы',
    providers: 'Не удалось загрузить поставщиков.',
    memory: 'API памяти недоступен.',
  },
} as const;