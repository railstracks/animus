export const memory = {
    title: 'Браузер пам\'яті',
    actions: {
      refresh: 'Оновити',
      triggerConsolidation: 'Запустіть консолідацію'
    },
    common: {
      notAvailable: 'n/a'
    },
    layers: {
      title: 'Шари',
      empty: 'Не знайдено шарів пам\'яті.',
      horizon: 'Горизонт',
      consolidationInterval: 'Інтервал консолідації',
      retentionPolicy: 'Політика утримання',
      perspectiveCurrent: 'Сучасна перспектива',
      perspectivePast: 'Минула перспектива',
      perspectiveFuture: 'Майбутня перспектива',
      viewObservations: 'Переглянути спостереження'
    },
    consolidation: {
      title: 'Консолідація',
      activeJob: 'Активна робота',
      lastRun: 'Останній біг',
      lastJob: 'Остання робота',
      promoted: 'Підвищений',
      demoted: 'Понижений',
      archived: 'Архівовано',
      state: {
        running: 'Біг',
        idle: 'Бездіяльність'
      }
    },
    list: {
      title: 'Спостереження',
      activeLayer: 'Шар: {layer}',
      sortBy: 'Сортувати',
      orderBy: 'порядок',
      tagFilter: 'Фільтрувати за тегами',
      pageSize: 'Розмір сторінки',
      compactMode: 'Компактний',
      openDetail: 'Подробиці',
      emptyLayer: 'У цьому шарі ще немає спостережень.',
      emptyFilter: 'Жодне спостереження не відповідає поточному фільтру тегів.',
      sort: {
        weight: 'вага',
        age: 'Вік',
        tags: 'Теги'
      },
      order: {
        desc: 'Спускається',
        asc: 'Висхідний'
      },
      columns: {
        content: 'Зміст',
        tags: 'Теги',
        weight: 'вага',
        age: 'Вік',
        decay: 'Розпад',
        actions: 'Дії'
      }
    },
    detail: {
      title: 'Деталь спостереження',
      empty: 'Виберіть спостереження для перевірки та редагування.',
      id: 'ID',
      content: 'Зміст',
      layer: 'Шар',

      timestamp: 'Мітка часу',
      demotionReason: 'Причина пониження',
      demotionTimestamp: 'Мітка часу пониження',
      editWeight: 'вага',
      editTags: 'Теги',
      save: 'зберегти',
      reset: 'Скинути',
      archive: 'Архів',
      saveSuccess: 'Спостереження оновлено.',
      archiveSuccess: 'Спостереження заархівовано.'
    }
  } as const;
