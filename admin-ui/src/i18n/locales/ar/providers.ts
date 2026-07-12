export const providers = {
    title: 'تكوين الموفر',
    actions: {
      refresh: 'تحديث',
      add: 'إضافة موفر',
      test: 'اختبار',
      edit: 'تحرير',
      delete: 'حذف',
      setDefault: 'تعيين الافتراضي',
      confirmDelete: 'هل ترغب في إزالة الموفر "{id}"؟ لا يمكن التراجع عن هذا.'
    },
    columns: {
      status: 'الحالة',
      provider: 'الاسم',
      type: 'اكتب',
      baseUrl: 'عنوان URL الأساسي',
      defaultModel: 'نموذج',
      default: 'الافتراضي',
      actions: 'الإجراءات'
    },
    empty: {
      title: 'لا مقدمي الخدمات',
      description: 'أضف مزود LLM للبدء.'
    },
    form: {
      createTitle: 'إضافة موفر',
      editTitle: 'تحرير الموفر',
      providerType: 'نوع المزود',
      instanceName: 'اسم المثيل',
      instanceNameHint: 'اسم فريد لتكوين الموفر هذا.',
      baseUrl: 'عنوان URL الأساسي',
      defaultModel: 'النموذج الافتراضي',
      defaultContextWindow: 'نافذة السياق الافتراضية',
      modelContextWindows: 'تجاوزات السياق لكل نموذج',
      modelContextWindowsHint: 'كائن JSON، على سبيل المثال. جي بي تي-5.2: 200000',
      modelContextWindowsInvalid: 'يجب أن تكون تجاوزات السياق لكل نموذج JSON صالحًا.',
      authType: 'نوع المصادقة',
      apiKey: 'مفتاح واجهة برمجة التطبيقات',
      apiKeyPlaceholder: 'اتركه فارغًا للاحتفاظ بالمفتاح الحالي',
      authFile: 'ملف المصادقة',
      oauthBrowserTitle: 'تسجيل دخول المتصفح (رمز التفويض)',
      oauthRedirectUri: 'إعادة توجيه URI',
      oauthStartBrowser: 'بدء تسجيل الدخول للمتصفح',
      oauthAuthorizationUrl: 'عنوان URL للتفويض',
      oauthState: 'الدولة',
      oauthCallbackUrl: 'الصق عنوان URL لرد الاتصال',
      oauthCallbackHint: 'الصق عنوان URL الكامل لرد الاتصال الذي يحتوي على الكود والحالة.',
      oauthCompleteBrowser: 'استكمال تسجيل الدخول للمتصفح',
      concurrency: 'ماكس التزامن',
      create: 'إنشاء',
      save: 'حفظ',
      cancel: 'إلغاء',
      modelManualHint: 'أدخل معرف النموذج يدويًا. قائمة النماذج غير متوفرة لهذا النوع من الموفر.',
      modelsLockedHint: 'احفظ الموفر باستخدام مفتاح API لإلغاء قفل اختيار النموذج.',
      savedContinue: 'تم حفظ الموفر. يمكنك الآن تحديد نموذج.',
      saveAndContinue: 'حفظ ومتابعة'
    },
    testSuccess: 'يمكن الوصول إلى الموفر "{id}".',
    testFailed: 'فشل اختبار الموفر "{id}".',
    createSuccess: 'تم إنشاء الموفر "{id}".',
    updateSuccess: 'تم تحديث الموفر "{id}".',
    deleteSuccess: 'تم حذف الموفر "{id}".',
    defaultSuccess: 'تم ضبط الموفر الافتراضي على "{id}".',
    errors: {
      loadFailed: 'فشل في تحميل مقدمي الخدمة'
    }
  } as const;
