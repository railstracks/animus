export const providers = {
    title: 'Конфігурація постачальника',
    actions: {
      refresh: 'Оновити',
      add: 'Додати постачальника',
      test: 'Тест',
      edit: 'Редагувати',
      delete: 'Видалити',
      setDefault: 'Установити за замовчуванням',
      confirmDelete: 'Видалити постачальника "{id}"? Це неможливо скасувати.'
    },
    columns: {
      status: 'Статус',
      provider: 'Ім\'я',
      type: 'Тип',
      baseUrl: 'Базовий URL',
      defaultModel: 'Модель',
      default: 'За замовчуванням',
      actions: 'Дії'
    },
    empty: {
      title: 'Немає постачальників',
      description: 'Щоб почати, додайте постачальника LLM.'
    },
    form: {
      createTitle: 'Додати постачальника',
      editTitle: 'Редагувати постачальника',
      providerType: 'Тип постачальника',
      instanceName: 'Ім\'я екземпляра',
      instanceNameHint: 'Унікальна назва для цієї конфігурації постачальника.',
      baseUrl: 'Базовий URL',
      defaultModel: 'Модель за замовчуванням',
      defaultContextWindow: 'Контекстне вікно за умовчанням',
      modelContextWindows: 'Перевизначення контексту для кожної моделі',
      modelContextWindowsHint: 'Об’єкт JSON, напр. gpt-5.2: 200000',
      modelContextWindowsInvalid: 'Перевизначення контексту для кожної моделі має бути дійсним JSON.',
      authType: 'Тип авторизації',
      apiKey: 'Ключ API',
      apiKeyPlaceholder: 'Залиште пустим, щоб зберегти поточний ключ',
      authFile: 'Файл авторизації',
      oauthBrowserTitle: 'Вхід у браузер (код авторизації)',
      oauthRedirectUri: 'URI перенаправлення',
      oauthStartBrowser: 'Запустіть вхід у браузері',
      oauthAuthorizationUrl: 'URL авторизації',
      oauthState: 'Держава',
      oauthCallbackUrl: 'Вставте URL зворотного виклику',
      oauthCallbackHint: 'Вставте повну URL-адресу зворотного виклику, яка містить код і стан.',
      oauthCompleteBrowser: 'Повний вхід у веб-переглядач',
      concurrency: 'Максимальний паралелізм',
      create: 'Створити',
      save: 'зберегти',
      cancel: 'Скасувати',
      modelManualHint: 'Введіть ідентифікатор моделі вручну. Перелік моделей недоступний для цього типу постачальника.',
      modelsLockedHint: 'Збережіть постачальника з ключем API, щоб розблокувати вибір моделі.',
      savedContinue: 'Провайдер збережено. Тепер можна вибрати модель.',
      saveAndContinue: 'Зберегти та продовжити'
    },
    testSuccess: 'Постачальник "{id}" доступний.',
    testFailed: 'Перевірка постачальника "{id}" не вдалася',
    createSuccess: 'Постачальник "{id}" створено.',
    updateSuccess: 'Постачальник "{id}" оновлено.',
    deleteSuccess: 'Постачальник "{id}" видалено.',
    defaultSuccess: 'Постачальником за умовчанням встановлено "{id}".',
    errors: {
      loadFailed: 'Не вдалося завантажити постачальників'
    }
  } as const;
