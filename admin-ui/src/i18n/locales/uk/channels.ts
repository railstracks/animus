export const channels = {
    title: 'Канали',
    empty: 'Канали не налаштовано. Додайте один, щоб почати.',
    createSuccess: 'Канал "{name}" успішно створено.',
    updateSuccess: 'Канал "{name}" успішно оновлено.',
    deleteSuccess: 'Канал "{name}" видалено.',
    columns: {
      name: 'Ім\'я',
      type: 'Тип',
      identity: 'Ідентичність',
      endpoint: 'Кінцева точка',
      enabled: 'Увімкнено',
      connected: 'Підключено',
      lastEvent: 'Остання подія',
      actions: 'Дії'
    },
    state: {
      connected: 'Підключено',
      disconnected: 'Відключено'
    },
    actions: {
      refresh: 'Оновити',
      add: 'Додати канал',
      cancel: 'Скасувати',
      create: 'Створити',
      save: 'зберегти',
      confirmDelete: 'Видалити канал "{name}"? Це неможливо скасувати.'
    },
    form: {
      createTitle: 'Додати канал',
      editTitle: 'Редагувати "{name}"',
      name: 'Назва каналу',
      type: 'Тип каналу',
      agent: 'Агент',
      minResponseInterval: 'Мінімальний інтервал відповіді (секунди)',
      allowInterjection: 'Дозволити втручання',
      irc: {
        host: 'Сервер IRC',
        port: 'Порт',
        serverPassword: 'Пароль сервера',
        nick: 'псевдонім',
        username: 'Ім\'я користувача',
        realname: 'Справжнє ім\'я',
        channels: 'Канали',
        channelsHint: 'По одному на рядок. Формат: #channel [ключ]',
        agent: 'Агент',
        respondToChannel: 'Відповідайте на повідомлення каналу',
        respondToDm: 'Відповідайте на прямі повідомлення',
        respondToNotices: 'Відповідайте на повідомлення',
        allowedDmUsers: 'Дозволені користувачі DM',
        reconnect: 'Автоматичне повторне підключення'
      },
      telegram: {
        botToken: 'Токен бота',
        tokenHint: 'Залиште поле порожнім, щоб зберегти існуючий маркер'
      },
      vk: {
        accessToken: 'Токен доступу до спільноти',
        groupId: 'ID групи'
      },
      bluesky: {
        handle: 'Ручка',
        appPassword: 'Пароль програми',
        pds: 'URL-адреса PDS'
      },
      mastodon: {
        handle: 'Ручка',
        instance: 'URL екземпляра'
      },
      email: {
        apiKey: 'Ключ API AgentMail',
        apiKeyHint: 'Залиште пустим, щоб зберегти поточний ключ',
        inboxId: 'ID папки "Вхідні".',
        pollingWait: 'Інтервал опитування (секунди)'
      },
      twitter: {
        tier: 'Рівень API',
        clientId: 'Ідентифікатор клієнта OAuth',
        clientSecret: 'Секрет клієнта OAuth',
        accessToken: 'Маркер доступу',
        tokenHint: 'Залиште поле порожнім, щоб зберегти існуючий маркер',
        refreshToken: 'Оновити маркер',
        authorize: 'Авторизуйтеся за допомогою Twitter',
        oauthStarted: 'Відкрилося вікно авторизації. Завершіть процес у своєму браузері.'
      },
      discord: {
        botToken: 'Токен бота',
        tokenHint: 'Залиште поле порожнім, щоб зберегти існуючий маркер',
        applicationId: 'Ідентифікатор програми (для команд із косою рискою)',
        monitoredChannels: 'Контрольовані канали',
        monitoredChannelsHint: 'Один ідентифікатор каналу на рядок. Бот повинен бути на сервері.',
        respondToDm: 'Відповідайте на DM',
        respondToMentions: 'Відповідайте на згадки',
        dmWhitelistEnabled: 'Обмежити ПП дозволеним користувачам',
        allowedDmUsers: 'Дозволені користувачі ПП',
        allowedDmUsersHint: 'Імена користувачів Discord (по одному на рядок). Тільки ці користувачі можуть писати боту в ПП.'
      },
      slack: {
        botToken: 'Токен бота (xoxb-)',
        tokenHint: 'Залиште поле порожнім, щоб зберегти існуючий маркер',
        appToken: 'Маркер програми (xapp-)',
        appTokenHint: 'Для режиму розетки (фаза 2). Необов’язковий для Фази 1.',
        monitoredChannels: 'Контрольовані канали (перевизначення)',
        monitoredChannelsHint: 'Бот автоматично відстежує всі канали, учасником яких є. Додайте сюди лише ідентифікатори, щоб обмежити область.',
        respondToMentions: "Відповідайте на {'@'}згадки",
        respondToAll: 'Відповідати на всі повідомлення (ігнорує фільтр згадок)',
        threadedReplies: 'Відповіді на ланцюжки в каналах (кожне повідомлення починає ланцюжок)'
      }
    }
  } as const;
