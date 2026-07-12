export const providers = {
    title: 'Конфигурация поставщика',
    actions: {
      refresh: 'Обновить',
      add: 'Добавить поставщика',
      test: 'Тест',
      edit: 'Редактировать',
      delete: 'Удалить',
      setDefault: 'Установить по умолчанию',
      confirmDelete: 'Удалить поставщика «{id}»? Это невозможно отменить.'
    },
    columns: {
      status: 'Статус',
      provider: 'Имя',
      type: 'Тип',
      baseUrl: 'Базовый URL',
      defaultModel: 'Модель',
      default: 'По умолчанию',
      actions: 'Действия'
    },
    empty: {
      title: 'Нет провайдеров',
      description: 'Чтобы начать, добавьте поставщика LLM.'
    },
    form: {
      createTitle: 'Добавить поставщика',
      editTitle: 'Изменить поставщика',
      providerType: 'Тип поставщика',
      instanceName: 'Имя экземпляра',
      instanceNameHint: 'Уникальное имя для этой конфигурации поставщика.',
      baseUrl: 'Базовый URL',
      defaultModel: 'Модель по умолчанию',
      defaultContextWindow: 'Контекстное окно по умолчанию',
      modelContextWindows: 'Переопределения контекста для каждой модели',
      modelContextWindowsHint: 'Объект JSON, например. gpt-5.2: 200000',
      modelContextWindowsInvalid: 'Переопределения контекста для каждой модели должны быть действительными в формате JSON.',
      authType: 'Тип аутентификации',
      apiKey: 'API-ключ',
      apiKeyPlaceholder: 'Оставьте пустым, чтобы сохранить текущий ключ',
      authFile: 'Файл аутентификации',
      oauthBrowserTitle: 'Вход в браузер (код авторизации)',
      oauthRedirectUri: 'URI перенаправления',
      oauthStartBrowser: 'Начать вход через браузер',
      oauthAuthorizationUrl: 'URL-адрес авторизации',
      oauthState: 'Государство',
      oauthCallbackUrl: 'Вставить URL обратного вызова',
      oauthCallbackHint: 'Вставьте полный URL-адрес обратного вызова, содержащий код и состояние.',
      oauthCompleteBrowser: 'Полный вход в браузер',
      concurrency: 'Макс. параллелизм',
      create: 'Создать',
      save: 'Сохранить',
      cancel: 'Отмена',
      modelManualHint: 'Введите идентификатор модели вручную. Список моделей недоступен для этого типа поставщика.',
      modelsLockedHint: 'Сохраните поставщика с помощью ключа API, чтобы разблокировать выбор модели.',
      savedContinue: 'Провайдер сохранен. Теперь вы можете выбрать модель.',
      saveAndContinue: 'Сохранить и продолжить'
    },
    testSuccess: 'Поставщик «{id}» доступен.',
    testFailed: 'Тест поставщика «{id}» не пройден.',
    createSuccess: 'Поставщик «{id}» создан.',
    updateSuccess: 'Провайдер "{id}" обновлен.',
    deleteSuccess: 'Провайдер "{id}" удален.',
    defaultSuccess: 'Поставщик по умолчанию установлен на «{id}».',
    errors: {
      loadFailed: 'Не удалось загрузить поставщиков.'
    }
  } as const;
