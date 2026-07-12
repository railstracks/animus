export const ontology = {
    title: 'مستكشف الأنطولوجيا',
    subtitle: 'تصفح الكيانات الدلالية وخصائصها المدعومة بالملاحظة.',
    actions: {
      refresh: 'تحديث'
    },
    sections: {
      agent: 'وكيل',
      categories: 'الفئات',
      entities: 'الكيانات',
      details: 'تفاصيل الكيان',
      properties: 'خصائص'
    },
    states: {
      new: 'جديد',
      current: 'الحالي',
      deprecated: 'مهمل'
    },
    empty: {
      entities: 'لم يتم العثور على كيانات لهذه الفئة.',
      details: 'حدد جهة لفحص خصائصها.'
    }
  } as const;
