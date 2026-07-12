export const chat = {
    sessionLabel: 'Сесія',
    newSession: 'Нова сесія',
    refresh: 'Оновити',
    stopGenerating: 'Зупинити генерацію',
    emptyTitle: 'Почніть розмову',
    emptyDescription: 'Запитайте Animus про пам’ять, конфігурацію чи живий сеанс, щоб почати.',
    streamState: {
      streaming: 'трансляція...',
      stopped: 'зупинився'
    },
    jumpToLatest: 'Перейти до останнього',
    composerPlaceholder: 'Надіслати повідомлення Анімусу...',
    adminTokenLabel: 'Маркер адміністратора (необов\'язково)',
    send: 'Надіслати',
    contextTitle: 'Контекст',
    context: {
      session: 'Сесія',
      layers: 'Шари',
      tools: 'Інструменти',
      budget: 'Бюджет',
      fallbackNote: 'Знімок контексту оновлюється в міру зміни сеансів.'
    },
    sidebarTabs: {
      context: 'Контекст',
      sessions: 'Сеанси',
      messages: 'повідомлення',
      noSessions: 'Сеансів ще немає.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'Напишіть повідомлення, щоб почати новий ланцюжок.'
    },
    thinking: {
      label: "Мислення"
    },
    toolResult: {
      label: "Інструмент Результат"
    },
    toolCall: {
      label: "Виклик інструменту"
    },
    reasoning: {
      label: 'Режим міркування',
      enabled: 'Увімкнено',
      disabled: 'Вимкнено',
      effort: 'зусилля',
      instructionLabel: 'Системна інструкція',
      instructionPlaceholder: 'Індивідуальна інструкція міркування (використовується за умовчанням, якщо пусто)',
      notSupported: 'Аргументація недоступна для цієї моделі.'
    },
    provider: {
      label: 'Провайдер',
      select: 'Провайдер',
      model: 'Модель'
    },
    agent: {
      label: 'Агент (новий сеанс)'
    },
    status: {
      closed: 'Закрито',
      connecting: 'Підключення',
      open: 'Підключено'
    },
    errors: {
      websocket: 'Помилка WebSocket',
      websocketNotConnected: 'WebSocket ще не підключено. Спробуйте ще раз.',
      agentNotReady: 'Вибір агента ще завантажується. Будь ласка, зачекайте трохи і спробуйте ще раз.',
      unknownWebsocket: 'Невідома помилка веб-сокета',
      sessionLoadFailed: 'Не вдалося завантажити сеанс {sessionId}: {message}'
    }
  } as const;
