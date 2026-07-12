export const ontology = {
    title: 'Ontoloji Gezgini',
    subtitle: 'Anlamsal varlıklara ve bunların gözlem destekli özelliklerine göz atın.',
    actions: {
      refresh: 'Yenile'
    },
    sections: {
      agent: 'Temsilci',
      categories: 'Kategoriler',
      entities: 'Varlıklar',
      details: 'Varlık Ayrıntıları',
      properties: 'Özellikler'
    },
    states: {
      new: 'yeni',
      current: 'akım',
      deprecated: 'kullanımdan kaldırıldı'
    },
    empty: {
      entities: 'Bu kategoriye ait varlık bulunamadı.',
      details: 'Özellikleri incelemek için bir varlık seçin.'
    }
  } as const;
