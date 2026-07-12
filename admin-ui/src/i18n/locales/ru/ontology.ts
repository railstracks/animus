export const ontology = {
    title: 'Обозреватель онтологий',
    subtitle: 'Просмотрите семантические объекты и их свойства, основанные на наблюдениях.',
    actions: {
      refresh: 'Обновить'
    },
    sections: {
      agent: 'Агент',
      categories: 'Категории',
      entities: 'Сущности',
      details: 'Подробности об объекте',
      properties: 'Свойства'
    },
    states: {
      new: 'новый',
      current: 'текущий',
      deprecated: 'устарел'
    },
    empty: {
      entities: 'Для этой категории объектов не найдено.',
      details: 'Выберите объект для проверки свойств.'
    }
  } as const;
