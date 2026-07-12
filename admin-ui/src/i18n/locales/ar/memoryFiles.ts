export const memoryFiles = {
    title: 'ملفات الذاكرة',
    subtitle: 'قم بتخزين عناصر النص الخام والبحث عبر مجالات الذاكرة.',
    common: {
      yes: 'نعم',
      no: 'لا'
    },
    actions: {
      refresh: 'تحديث',
      open: 'مفتوح',
      delete: 'حذف',
      process: 'قائمة الانتظار للمعالجة',
      import: 'استيراد',
      importBatch: 'دفعة الاستيراد',
      saveMetadata: 'حفظ البيانات الوصفية',
      search: 'بحث'
    },
    fields: {
      sourcePath: 'مسار المصدر',
      fileType: 'نوع الملف',
      contentMutable: 'المحتوى قابل للتغيير',
      agentId: 'معرف الوكيل',
      status: 'الحالة',
      agentFilter: 'التصفية حسب الوكيل',
      allAgents: 'جميع الوكلاء',
      superseded: 'تم استبداله',
      content: 'المحتوى'
    },
    types: {
      all: 'جميع الأنواع',
      expanded_memory: 'الذاكرة الموسعة',
      session_log: 'سجل الجلسة',
      daily_note: 'ملاحظة يومية',
      bootstrap_file: 'ملف بوتستراب',
      journal: 'مجلة',
      external_doc: 'وثيقة خارجية'
    },
    stats: {
      title: 'احصائيات'
    },
    list: {
      title: 'قائمة الملفات',
      typeFilter: 'نوع التصفية',
      limit: 'الحد',
      offset: 'إزاحة',
      columns: {
        id: 'معرف',
        type: 'اكتب',
        sourcePath: 'مسار المصدر',
        contentMutable: 'قابل للتغيير',
        agentId: 'معرف الوكيل',
      status: 'الحالة',
      agentFilter: 'التصفية حسب الوكيل',
      allAgents: 'جميع الوكلاء',
        superseded: 'تم استبداله',
        importedAt: 'مستورد',
        actions: 'الإجراءات'
      },
      status: {
        unprocessed: 'غير المجهزة',
        processed: 'تمت معالجتها'
      }
    },
    import: {
      singleTitle: 'ملف الاستيراد',
      selectFile: 'حدد الملف',
      selected: 'مختارة',
      batchTitle: 'استيراد دفعة',
      selectFiles: 'حدد الملفات',
      filesSelected: 'الملفات المختارة'
    },
    detail: {
      title: 'تفاصيل الملف',
      empty: 'حدد ملفًا من القائمة لفحص البيانات التعريفية وتحريرها.',
      createdAt: 'تم إنشاؤها',
      importedAt: 'مستورد',
      contentTitle: 'المحتوى',
      contentImmutableNotice: 'تم تعطيل تحرير المحتوى لأنه تم وضع علامة على هذا الملف بأنه غير قابل للتغيير.'
    },
    search: {
      title: 'البحث الموحد',
      query: 'استعلام البحث',
      limit: 'الحد',
      relevance: 'الصلة',
      empty: 'لا توجد نتائج بحث حتى الآن.',
      domains: {
        observation: 'الملاحظة',
        observations: 'الملاحظات',
        ontology: 'الوجود',
        memory_file: 'ملفات',
        sessions: 'الجلسات'
      }
    },
    errors: {
      loadFiles: 'فشل تحميل ملفات الذاكرة.',
      loadDetail: 'فشل تحميل تفاصيل الملف.',
      importRequired: 'حدد ملفًا لاستيراده.',
      importSingle: 'فشل في استيراد ملف الذاكرة.',
      batchRequired: 'حدد ملفًا واحدًا على الأقل للاستيراد الدفعي.',
      importBatch: 'فشل استيراد الحمولة الدفعية.',
      updateMetadata: 'فشل تحديث بيانات التعريف.',
      delete: 'فشل في حذف ملف الذاكرة.',
      search: 'فشل البحث في الذاكرة.'
    }
  } as const;
