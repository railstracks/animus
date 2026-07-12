export const interfaces = {
    title: 'Управление интерфейсами',
    actions: {
      refresh: 'Обновить',
      add: 'Добавить интерфейс',
      edit: 'Изменить',
      enable: 'Включить',
      disable: 'Отключить',
      delete: 'Удалить',
      confirmDelete: 'Удалить интерфейс "{name}"?'
    },
    columns: {
      name: 'Имя',
      type: 'Тип',
      module: 'ID модуля',
      enabled: 'Включён',
      connected: 'Подключён',
      lastEvent: 'Последнее событие',
      actions: 'Действия'
    },
    state: {
      enabled: 'да',
      disabled: 'нет',
      connected: 'подключён',
      disconnected: 'отключён'
    },
    empty: 'Интерфейсы пока не настроены.',
    form: {
      createTitle: 'Создать интерфейс',
      editTitle: 'Изменить интерфейс: {name}',
      name: 'Имя экземпляра',
      type: 'Тип интерфейса',
      moduleId: 'ID модуля',
      enabled: 'Включён',
      configJson: 'JSON конфигурации',
      create: 'Создать',
      save: 'Сохранить',
      cancel: 'Отмена',
      irc: {
        host: 'Хост',
        port: 'Порт',
        nick: 'Ник',
        serverPassword: 'Пароль сервера',
        username: 'Имя пользователя',
        realname: 'Настоящее имя',
        dmOnly: 'Режим только личных сообщений',
        respondToChannel: 'Отвечать на активность в каналах',
        respondToDm: 'Отвечать на личные сообщения',
        respondToNotices: 'Отвечать на уведомления',
        allowedDmUsers: 'Разрешённые пользователи ЛС',
        allowedDmUsersHint: 'Необязательный список разрешений; через запятую или с новой строки.',
        agentId: 'ID агента',
        reconnectEnabled: 'Повторное подключение включено',
        reconnectInitialDelay: 'Начальная задержка повторного подключения (мс)',
        reconnectMaxDelay: 'Макс. задержка повторного подключения (мс)'
      }
    },
    createSuccess: 'Интерфейс "{name}" создан.',
    updateSuccess: 'Интерфейс "{name}" обновлён.',
    deleteSuccess: 'Интерфейс "{name}" удалён.'
  } as const;
