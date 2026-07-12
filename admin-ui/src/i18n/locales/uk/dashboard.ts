export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'Ласкаво просимо в Анімус.',
    welcomeSub: 'Налаштуйте свого першого агента, щоб почати.',
    beginSetup: 'Почати налаштування →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'Час роботи',
    agents: 'Агенти',
    sessions: 'Сеанси',
    providers: 'Провайдери',
    errors: 'помилка',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'Останні сесії',
    viewAll: 'Переглянути всі',
    empty: 'Сесій ще немає.',
    startChat: 'Розпочати чат →',
    messages: 'повідомлення',
  },
  // Memory card
  memory: {
    title: 'Пам\'ять',
    inspect: 'Оглянути',
    empty: 'Немає налаштованих рівнів пам’яті.',
    totalObservations: 'сукупні спостереження',
  },
  // Provider card
  providers: {
    title: 'Провайдери',
    manage: 'Керувати',
    empty: 'Постачальники не налаштовані.',
    runWizard: 'Запустіть майстер налаштування →',
  },
  // Quick Actions card
  quickActions: {
    title: 'Швидкі дії',
    newChat: 'Новий чат',
    addAgent: 'Додати агента',
    provider: 'Провайдер',
    logs: 'Журнали',
    scheduler: 'Планувальник',
    memory: 'Пам\'ять',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'Інформація про демон',
    status: 'Статус',
    uptime: 'Час роботи',
    agents: 'Агенти',
    activeSessions: 'Активні сесії',
    providers: 'Провайдери',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'Шари пам\'яті',
    empty: 'Немає налаштованих рівнів пам’яті.',
  },
  // State labels
  state: {
    loading: 'завантаження',
    unknown: 'невідомий',
  },
  // Error messages
  errors: {
    sessions: 'Не вдалося завантажити сеанси',
    providers: 'Не вдалося завантажити постачальників',
    memory: 'API пам\'яті недоступний',
  },
} as const;