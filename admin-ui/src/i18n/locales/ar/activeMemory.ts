export const activeMemory = {
    title: 'الذاكرة النشطة',
    subtitle: 'سياق الوكيل المجمع – ما يراه الوكيل في ديباجته',
    empty: 'حدد وكيلًا لعرض السياق المجمع الخاص به.',
    actions: {
      refresh: 'تحديث'
    },
    labels: {
      agent: 'وكيل',
      session: 'جلسة',
      syntheticSession: 'الاصطناعية (جلسة فارغة للاختبار)',
      blocks: 'كتل'
    },
    flags: {
      synthetic: 'جلسة اصطناعية',
      live: 'جلسة حية'
    },
    sections: {
      rendered: 'الإخراج المقدم',
      blocks: 'انهيار الكتلة'
    }
  } as const;
