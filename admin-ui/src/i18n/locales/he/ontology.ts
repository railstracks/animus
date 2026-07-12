export const ontology = {
    title: 'סייר אונטולוגיה',
    subtitle: 'עיין בישויות סמנטיות ובמאפיינים המגובים בתצפית.',
    actions: {
      refresh: 'רענן'
    },
    sections: {
      agent: 'סוכן',
      categories: 'קטגוריות',
      entities: 'ישויות',
      details: 'פרטי ישות',
      properties: 'נכסים'
    },
    states: {
      new: 'חדש',
      current: 'הנוכחי',
      deprecated: 'הוצא משימוש'
    },
    empty: {
      entities: 'לא נמצאו ישויות עבור קטגוריה זו.',
      details: 'בחר ישות לבדיקת נכסים.'
    }
  } as const;
