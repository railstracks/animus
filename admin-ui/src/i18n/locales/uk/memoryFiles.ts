export const memoryFiles = {
    title: 'Файли пам\'яті',
    subtitle: 'Зберігайте необроблені текстові артефакти та шукайте в доменах пам’яті.',
    common: {
      yes: 'так',
      no: 'немає'
    },
    actions: {
      refresh: 'Оновити',
      open: 'відкритий',
      delete: 'Видалити',
      process: 'Черга на обробку',
      import: 'Імпорт',
      importBatch: 'Імпорт партії',
      saveMetadata: 'Зберегти метадані',
      search: 'Пошук'
    },
    fields: {
      sourcePath: 'Вихідний шлях',
      fileType: 'Тип файлу',
      contentMutable: 'Змінний вміст',
      agentId: 'ID агента',
      status: 'Статус',
      agentFilter: 'Фільтрувати за агентом',
      allAgents: 'Всі агенти',
      superseded: 'Замінено',
      content: 'Зміст'
    },
    types: {
      all: 'Всі типи',
      expanded_memory: 'Розширена пам\'ять',
      session_log: 'Журнал сеансу',
      daily_note: 'Щоденна нотатка',
      bootstrap_file: 'Файл початкового завантаження',
      journal: 'журнал',
      external_doc: 'Зовнішній док'
    },
    stats: {
      title: 'статистика'
    },
    list: {
      title: 'Список файлів',
      typeFilter: 'Тип фільтра',
      limit: 'Ліміт',
      offset: 'Зсув',
      columns: {
        id: 'ID',
        type: 'Тип',
        sourcePath: 'Вихідний шлях',
        contentMutable: 'Змінний',
        agentId: 'ID агента',
      status: 'Статус',
      agentFilter: 'Фільтрувати за агентом',
      allAgents: 'Всі агенти',
        superseded: 'Замінено',
        importedAt: 'Імпортні',
        actions: 'Дії'
      },
      status: {
        unprocessed: 'Необроблений',
        processed: 'Оброблено'
      }
    },
    import: {
      singleTitle: 'Імпорт файлу',
      selectFile: 'Виберіть файл',
      selected: 'Вибране',
      batchTitle: 'Пакетний імпорт',
      selectFiles: 'Виберіть файли',
      filesSelected: 'вибраних файлів'
    },
    detail: {
      title: 'Деталі файлу',
      empty: 'Виберіть файл зі списку, щоб перевірити та відредагувати метадані.',
      createdAt: 'Створено',
      importedAt: 'Імпортні',
      contentTitle: 'Зміст',
      contentImmutableNotice: 'Редагування вмісту вимкнено, оскільки цей файл позначено як незмінний.'
    },
    search: {
      title: 'Уніфікований пошук',
      query: 'Пошуковий запит',
      limit: 'Ліміт',
      relevance: 'Актуальність',
      empty: 'Результатів пошуку ще немає.',
      domains: {
        observation: 'Спостереження',
        observations: 'Спостереження',
        ontology: 'Онтологія',
        memory_file: 'Файли',
        sessions: 'Сеанси'
      }
    },
    errors: {
      loadFiles: 'Не вдалося завантажити файли пам\'яті.',
      loadDetail: 'Не вдалося завантажити деталі файлу.',
      importRequired: 'Виберіть файл для імпорту.',
      importSingle: 'Не вдалося імпортувати файл пам’яті.',
      batchRequired: 'Виберіть принаймні один файл для пакетного імпорту.',
      importBatch: 'Не вдалося імпортувати пакетне корисне навантаження.',
      updateMetadata: 'Не вдалося оновити метадані.',
      delete: 'Не вдалося видалити файл пам’яті.',
      search: 'Помилка пошуку в пам\'яті.'
    }
  } as const;
