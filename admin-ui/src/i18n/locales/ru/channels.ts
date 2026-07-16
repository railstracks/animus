export const channels = {
    title: 'Каналы',
    empty: 'Каналы не настроены. Добавьте один, чтобы начать.',
    createSuccess: 'Канал «{name}» успешно создан.',
    updateSuccess: 'Канал «{name}» успешно обновлен.',
    deleteSuccess: 'Канал «{name}» удален.',
    columns: {
      name: 'Имя',
      type: 'Тип',
      identity: 'идентичность',
      endpoint: 'Конечная точка',
      enabled: 'Включено',
      connected: 'Подключено',
      lastEvent: 'Последнее событие',
      actions: 'Действия'
    },
    state: {
      connected: 'Подключено',
      disconnected: 'Отключено'
    },
    actions: {
      refresh: 'Обновить',
      add: 'Добавить канал',
      cancel: 'Отмена',
      create: 'Создать',
      save: 'Сохранить',
      confirmDelete: 'Удалить канал "{name}"? Это невозможно отменить.'
    },
    form: {
      createTitle: 'Добавить канал',
      editTitle: 'Изменить "{name}"',
      name: 'Название канала',
      type: 'Тип канала',
      agent: 'Агент',
      minResponseInterval: 'Минимальный интервал ответа (секунды)',
      allowInterjection: 'Разрешить вставки',
      irc: {
        host: 'IRC-сервер',
        port: 'Порт',
        serverPassword: 'Пароль сервера',
        nick: 'Псевдоним',
        username: 'Имя пользователя',
        realname: 'Настоящее имя',
        channels: 'Каналы',
        channelsHint: 'По одному на строку. Формат: #channel [ключ]',
        agent: 'Агент',
        respondToChannel: 'Отвечайте на сообщения канала',
        respondToDm: 'Отвечайте на сообщения в Директе',
        respondToNotices: 'Реагировать на уведомления',
        allowedDmUsers: 'Разрешенные пользователи DM',
        reconnect: 'Автоматическое повторное подключение'
      },
      telegram: {
        botToken: 'Токен бота',
        tokenHint: 'Оставьте пустым, чтобы сохранить существующий токен.'
      },
      vk: {
        accessToken: 'Токен доступа сообщества',
        groupId: 'Идентификатор группы'
      },
      bluesky: {
        handle: 'Ручка',
        appPassword: 'Пароль приложения',
        pds: 'URL-адрес PDS'
      },
      mastodon: {
        handle: 'Ручка',
        instance: 'URL-адрес экземпляра'
      },
      email: {
        apiKey: 'API-ключ агента почты',
        apiKeyHint: 'Оставьте пустым, чтобы сохранить текущий ключ',
        inboxId: 'Идентификатор входящего почтового ящика',
        pollingWait: 'Интервал опроса (секунды)'
      },
      twitter: {
        tier: 'Уровень API',
        clientId: 'Идентификатор клиента OAuth',
        clientSecret: 'Секрет клиента OAuth',
        accessToken: 'Токен доступа',
        tokenHint: 'Оставьте пустым, чтобы сохранить существующий токен.',
        refreshToken: 'Обновить токен',
        authorize: 'Авторизоваться через Твиттер',
        oauthStarted: 'Открылось окно авторизации. Завершите процесс в браузере.'
      },
      discord: {
        botToken: 'Токен бота',
        tokenHint: 'Оставьте пустым, чтобы сохранить существующий токен.',
        applicationId: 'Идентификатор приложения (для команд с косой чертой)',
        monitoredChannels: 'Контролируемые каналы',
        monitoredChannelsHint: 'Один идентификатор канала на строку. Бот должен быть на сервере.',
        respondToDm: 'Ответить в личные сообщения',
        respondToMentions: 'Отвечайте на упоминания',
        dmWhitelistEnabled: 'Ограничить ЛС разрешённым пользователям',
        allowedDmUsers: 'Разрешённые пользователи ЛС',
        allowedDmUsersHint: 'Имена пользователей Discord (по одному в строке). Только эти пользователи могут писать боту в ЛС.'
      },
      slack: {
        botToken: 'Токен бота (xoxb-)',
        tokenHint: 'Оставьте пустым, чтобы сохранить существующий токен.',
        appToken: 'Токен приложения (xapp-)',
        appTokenHint: 'Для режима розетки (фаза 2). Необязательно для этапа 1.',
        monitoredChannels: 'Контролируемые каналы (переопределить)',
        monitoredChannelsHint: 'Бот автоматически отслеживает все каналы, в которых он участвует. Добавляйте сюда только идентификаторы, чтобы ограничить область действия.',
        respondToMentions: "Отвечайте на упоминания {'@'}",
        respondToAll: 'Отвечать на все сообщения (игнорирует фильтр упоминаний)',
        threadedReplies: 'Ответы на темы в каналах (каждое сообщение запускает тему)'
      }
    }
  } as const;
