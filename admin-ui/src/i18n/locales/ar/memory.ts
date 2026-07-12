export const memory = {
    title: 'متصفح الذاكرة',
    actions: {
      refresh: 'تحديث',
      triggerConsolidation: 'تشغيل الدمج'
    },
    common: {
      notAvailable: 'غير متوفر'
    },
    layers: {
      title: 'طبقات',
      empty: 'لم يتم العثور على طبقات الذاكرة.',
      horizon: 'الأفق',
      consolidationInterval: 'الفاصل الزمني للتوحيد',
      retentionPolicy: 'سياسة الاحتفاظ',
      perspectiveCurrent: 'المنظور الحالي',
      perspectivePast: 'المنظور الماضي',
      perspectiveFuture: 'منظور المستقبل',
      viewObservations: 'عرض الملاحظات'
    },
    consolidation: {
      title: 'التوحيد',
      activeJob: 'الوظيفة النشطة',
      lastRun: 'التشغيل الأخير',
      lastJob: 'الوظيفة الأخيرة',
      promoted: 'تمت ترقيته',
      demoted: 'تم تخفيض رتبته',
      archived: 'مؤرشف',
      state: {
        running: 'الجري',
        idle: 'خامل'
      }
    },
    list: {
      title: 'الملاحظات',
      activeLayer: 'الطبقة: {layer}',
      sortBy: 'فرز',
      orderBy: 'النظام',
      tagFilter: 'تصفية حسب العلامات',
      pageSize: 'حجم الصفحة',
      compactMode: 'مدمج',
      openDetail: 'التفاصيل',
      emptyLayer: 'لا توجد ملاحظات في هذه الطبقة حتى الآن.',
      emptyFilter: 'لا توجد ملاحظات تتطابق مع مرشح العلامات الحالي.',
      sort: {
        weight: 'الوزن',
        age: 'العمر',
        tags: 'العلامات'
      },
      order: {
        desc: 'تنازلي',
        asc: 'تصاعدي'
      },
      columns: {
        content: 'المحتوى',
        tags: 'العلامات',
        weight: 'الوزن',
        age: 'العمر',
        decay: 'الاضمحلال',
        actions: 'الإجراءات'
      }
    },
    detail: {
      title: 'تفاصيل المراقبة',
      empty: 'حدد ملاحظة لفحصها وتحريرها.',
      id: 'معرف',
      content: 'المحتوى',
      layer: 'طبقة',

      timestamp: 'الطابع الزمني',
      demotionReason: 'سبب التخفيض',
      demotionTimestamp: 'الطابع الزمني للتخفيض',
      editWeight: 'الوزن',
      editTags: 'العلامات',
      save: 'حفظ',
      reset: 'إعادة تعيين',
      archive: 'أرشيف',
      saveSuccess: 'تم تحديث الملاحظة.',
      archiveSuccess: 'تم أرشفة الملاحظة.'
    }
  } as const;
