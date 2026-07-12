export const memory = {
    title: 'Браузер памяти',
    actions: {
      refresh: 'Обновить',
      triggerConsolidation: 'Запустить консолидацию'
    },
    common: {
      notAvailable: 'н/д'
    },
    layers: {
      title: 'Слои',
      empty: 'Слои памяти не найдены.',
      horizon: 'Горизонт',
      consolidationInterval: 'Интервал консолидации',
      retentionPolicy: 'Политика хранения',
      perspectiveCurrent: 'Текущая перспектива',
      perspectivePast: 'Прошлая перспектива',
      perspectiveFuture: 'Будущая перспектива',
      viewObservations: 'Просмотр наблюдений'
    },
    consolidation: {
      title: 'Консолидация',
      activeJob: 'Активная работа',
      lastRun: 'Последний запуск',
      lastJob: 'Последняя работа',
      promoted: 'Продвинутый',
      demoted: 'понижен в должности',
      archived: 'В архиве',
      state: {
        running: 'Бег',
        idle: 'Простой'
      }
    },
    list: {
      title: 'Наблюдения',
      activeLayer: 'Слой: {layer}',
      sortBy: 'Сортировать',
      orderBy: 'Заказать',
      tagFilter: 'Фильтровать по тегам',
      pageSize: 'Размер страницы',
      compactMode: 'Компактный',
      openDetail: 'Подробности',
      emptyLayer: 'Наблюдений в этом слое пока нет.',
      emptyFilter: 'Нет наблюдений, соответствующих текущему фильтру тегов.',
      sort: {
        weight: 'Вес',
        age: 'Возраст',
        tags: 'Теги'
      },
      order: {
        desc: 'По убыванию',
        asc: 'восходящий'
      },
      columns: {
        content: 'Содержание',
        tags: 'Теги',
        weight: 'Вес',
        age: 'Возраст',
        decay: 'Распад',
        actions: 'Действия'
      }
    },
    detail: {
      title: 'Детали наблюдения',
      empty: 'Выберите наблюдение для проверки и редактирования.',
      id: 'идентификатор',
      content: 'Содержание',
      layer: 'Слой',

      timestamp: 'Временная метка',
      demotionReason: 'Причина понижения в должности',
      demotionTimestamp: 'Временная метка понижения в должности',
      editWeight: 'Вес',
      editTags: 'Теги',
      save: 'Сохранить',
      reset: 'Сброс',
      archive: 'Архив',
      saveSuccess: 'Наблюдение обновлено.',
      archiveSuccess: 'Наблюдение заархивировано.'
    }
  } as const;
