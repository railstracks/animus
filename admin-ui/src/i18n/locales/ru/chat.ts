export const chat = {
    sessionLabel: 'Сессия',
    newSession: 'Новая сессия',
    refresh: 'Обновить',
    stopGenerating: 'Прекратить генерировать',
    emptyTitle: 'Начать разговор',
    emptyDescription: 'Спросите Animus о памяти, конфигурации или живом сеансе, чтобы начать работу.',
    streamState: {
      streaming: 'потоковая передача...',
      stopped: 'остановился'
    },
    jumpToLatest: 'Перейти к последней версии',
    composerPlaceholder: 'Отправить сообщение Анимусу...',
    adminTokenLabel: 'Токен администратора (необязательно)',
    send: 'Отправить',
    contextTitle: 'Контекст',
    context: {
      session: 'Сессия',
      layers: 'Слои',
      tools: 'Инструменты',
      budget: 'Бюджет',
      fallbackNote: 'Снимок контекста обновляется по мере изменения сеансов.'
    },
    sidebarTabs: {
      context: 'Контекст',
      sessions: 'Сессии',
      messages: 'сообщения',
      noSessions: 'Пока нет доступных сеансов.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'Напишите сообщение, чтобы начать новую тему.'
    },
    thinking: {
      label: "мышление"
    },
    toolResult: {
      label: "Результат инструмента"
    },
    toolCall: {
      label: "Вызов инструмента"
    },
    reasoning: {
      label: 'Режим рассуждения',
      enabled: 'Вкл.',
      disabled: 'Выкл.',
      effort: 'Усилие',
      instructionLabel: 'Системная инструкция',
      instructionPlaceholder: 'Пользовательская инструкция рассуждения (если пусто, используется значение по умолчанию)',
      notSupported: 'Для этой модели рассуждения недоступны.'
    },
    provider: {
      label: 'Поставщик',
      select: 'Поставщик',
      model: 'Модель'
    },
    agent: {
      label: 'Агент (новый сеанс)'
    },
    status: {
      closed: 'Закрыто',
      connecting: 'Подключение',
      open: 'Подключено'
    },
    errors: {
      websocket: 'Ошибка веб-сокета',
      websocketNotConnected: 'WebSocket еще не подключен. Пожалуйста, попробуйте еще раз.',
      agentNotReady: 'Выбор агента все еще загружается. Пожалуйста, подождите немного и повторите попытку.',
      unknownWebsocket: 'Неизвестная ошибка веб-сокета',
      sessionLoadFailed: 'Не удалось загрузить сеанс {sessionId}: {message}.'
    }
  } as const;
