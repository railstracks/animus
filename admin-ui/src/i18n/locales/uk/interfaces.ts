export const interfaces = {
    title: 'Керування інтерфейсами',
    actions: {
      refresh: 'Оновити',
      add: 'Додати інтерфейс',
      edit: 'Редагувати',
      enable: 'Увімкнути',
      disable: 'Вимкнути',
      delete: 'Видалити',
      confirmDelete: 'Видалити інтерфейс "{name}"?'
    },
    columns: {
      name: 'Назва',
      type: 'Тип',
      module: 'ID модуля',
      enabled: 'Увімкнено',
      connected: 'Підключено',
      lastEvent: 'Остання подія',
      actions: 'Дії'
    },
    state: {
      enabled: 'так',
      disabled: 'ні',
      connected: 'підключено',
      disconnected: 'відключено'
    },
    empty: 'Інтерфейси ще не налаштовано.',
    form: {
      createTitle: 'Створити інтерфейс',
      editTitle: 'Редагувати інтерфейс: {name}',
      name: 'Назва екземпляра',
      type: 'Тип інтерфейсу',
      moduleId: 'ID модуля',
      enabled: 'Увімкнено',
      configJson: 'JSON конфігурації',
      create: 'Створити',
      save: 'Зберегти',
      cancel: 'Скасувати',
      irc: {
        host: 'Хост',
        port: 'Порт',
        nick: 'Нік',
        serverPassword: 'Пароль сервера',
        username: 'Імʼя користувача',
        realname: 'Справжнє імʼя',
        dmOnly: 'Режим лише приватних повідомлень',
        respondToChannel: 'Відповідати на активність у каналах',
        respondToDm: 'Відповідати на приватні повідомлення',
        respondToNotices: 'Відповідати на сповіщення',
        allowedDmUsers: 'Дозволені користувачі приватних повідомлень',
        allowedDmUsersHint: 'Необовʼязковий список дозволених; розділяйте комами або новими рядками.',
        agentId: 'ID агента',
        reconnectEnabled: 'Повторне підключення увімкнено',
        reconnectInitialDelay: 'Початкова затримка повторного підключення (мс)',
        reconnectMaxDelay: 'Макс. затримка повторного підключення (мс)'
      }
    },
    createSuccess: 'Інтерфейс "{name}" створено.',
    updateSuccess: 'Інтерфейс "{name}" оновлено.',
    deleteSuccess: 'Інтерфейс "{name}" видалено.'
  } as const;
