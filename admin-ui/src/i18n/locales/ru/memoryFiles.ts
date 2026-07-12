export const memoryFiles = {
    title: 'Файлы памяти',
    subtitle: 'Храните необработанные текстовые артефакты и осуществляйте поиск по доменам памяти.',
    common: {
      yes: 'да',
      no: 'нет'
    },
    actions: {
      refresh: 'Обновить',
      open: 'Открыть',
      delete: 'Удалить',
      process: 'Очередь на обработку',
      import: 'Импорт',
      importBatch: 'Импорт пакета',
      saveMetadata: 'Сохранить метаданные',
      search: 'Поиск'
    },
    fields: {
      sourcePath: 'Исходный путь',
      fileType: 'Тип файла',
      contentMutable: 'Изменяемый контент',
      agentId: 'Идентификатор агента',
      status: 'Статус',
      agentFilter: 'Фильтровать по агенту',
      allAgents: 'Все агенты',
      superseded: 'Заменено',
      content: 'Содержание'
    },
    types: {
      all: 'Все типы',
      expanded_memory: 'Расширенная память',
      session_log: 'Журнал сеанса',
      daily_note: 'Ежедневная заметка',
      bootstrap_file: 'Файл начальной загрузки',
      journal: 'Журнал',
      external_doc: 'Внешний документ'
    },
    stats: {
      title: 'Статистика'
    },
    list: {
      title: 'Список файлов',
      typeFilter: 'Тип Фильтр',
      limit: 'Лимит',
      offset: 'Смещение',
      columns: {
        id: 'идентификатор',
        type: 'Тип',
        sourcePath: 'Исходный путь',
        contentMutable: 'Изменяемый',
        agentId: 'Идентификатор агента',
      status: 'Статус',
      agentFilter: 'Фильтровать по агенту',
      allAgents: 'Все агенты',
        superseded: 'Заменено',
        importedAt: 'Импортировано',
        actions: 'Действия'
      },
      status: {
        unprocessed: 'Необработанный',
        processed: 'Обработано'
      }
    },
    import: {
      singleTitle: 'Импортировать файл',
      selectFile: 'Выберите файл',
      selected: 'Выбрано',
      batchTitle: 'Пакетный импорт',
      selectFiles: 'Выберите файлы',
      filesSelected: 'выбранные файлы'
    },
    detail: {
      title: 'Детали файла',
      empty: 'Выберите файл из списка для проверки и редактирования метаданных.',
      createdAt: 'Создано',
      importedAt: 'Импортировано',
      contentTitle: 'Содержание',
      contentImmutableNotice: 'Редактирование содержимого отключено, поскольку этот файл помечен как неизменяемый.'
    },
    search: {
      title: 'Единый поиск',
      query: 'Поисковый запрос',
      limit: 'Лимит',
      relevance: 'Актуальность',
      empty: 'Результаты поиска пока отсутствуют.',
      domains: {
        observation: 'Наблюдение',
        observations: 'Наблюдения',
        ontology: 'Онтология',
        memory_file: 'Файлы',
        sessions: 'Сессии'
      }
    },
    errors: {
      loadFiles: 'Не удалось загрузить файлы памяти.',
      loadDetail: 'Не удалось загрузить сведения о файле.',
      importRequired: 'Выберите файл для импорта.',
      importSingle: 'Не удалось импортировать файл памяти.',
      batchRequired: 'Выберите хотя бы один файл для пакетного импорта.',
      importBatch: 'Не удалось импортировать пакетную полезную нагрузку.',
      updateMetadata: 'Не удалось обновить метаданные.',
      delete: 'Не удалось удалить файл памяти.',
      search: 'Поиск в памяти не удался.'
    }
  } as const;
