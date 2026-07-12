export const ontology = {
    title: 'Провідник онтології',
    subtitle: 'Перегляньте семантичні сутності та їхні властивості, підтверджені спостереженнями.',
    actions: {
      refresh: 'Оновити'
    },
    sections: {
      agent: 'Агент',
      categories: 'Категорії',
      entities: 'Сутності',
      details: 'Відомості про сутність',
      properties: 'Властивості'
    },
    states: {
      new: 'новий',
      current: 'поточний',
      deprecated: 'застарілий'
    },
    empty: {
      entities: 'Для цієї категорії не знайдено об’єктів.',
      details: 'Виберіть сутність для перевірки властивостей.'
    }
  } as const;
