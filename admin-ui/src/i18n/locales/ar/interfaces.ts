export const interfaces = {
    title: 'إدارة الواجهات',
    actions: {
      refresh: 'تحديث',
      add: 'إضافة واجهة',
      edit: 'تعديل',
      enable: 'تمكين',
      disable: 'تعطيل',
      delete: 'حذف',
      confirmDelete: 'حذف الواجهة "{name}"؟'
    },
    columns: {
      name: 'الاسم',
      type: 'النوع',
      module: 'معرّف الوحدة',
      enabled: 'ممكّنة',
      connected: 'متصلة',
      lastEvent: 'آخر حدث',
      actions: 'الإجراءات'
    },
    state: {
      enabled: 'نعم',
      disabled: 'لا',
      connected: 'متصلة',
      disconnected: 'غير متصلة'
    },
    empty: 'لا توجد واجهات معدّة بعد.',
    form: {
      createTitle: 'إنشاء واجهة',
      editTitle: 'تعديل الواجهة: {name}',
      name: 'اسم المثيل',
      type: 'نوع الواجهة',
      moduleId: 'معرّف الوحدة',
      enabled: 'ممكّنة',
      configJson: 'إعدادات JSON',
      create: 'إنشاء',
      save: 'حفظ',
      cancel: 'إلغاء',
      irc: {
        host: 'المضيف',
        port: 'المنفذ',
        nick: 'الاسم المستعار',
        serverPassword: 'كلمة مرور الخادم',
        username: 'اسم المستخدم',
        realname: 'الاسم الحقيقي',
        dmOnly: 'وضع الرسائل المباشرة فقط',
        respondToChannel: 'الرد على نشاط القناة',
        respondToDm: 'الرد على الرسائل المباشرة',
        respondToNotices: 'الرد على الإشعارات',
        allowedDmUsers: 'مستخدمو الرسائل المباشرة المسموحون',
        allowedDmUsersHint: 'قائمة سماح اختيارية؛ مفصولة بفواصل أو بأسطر جديدة.',
        agentId: 'معرّف الوكيل',
        reconnectEnabled: 'إعادة الاتصال مفعّلة',
        reconnectInitialDelay: 'تأخير إعادة الاتصال الأولي (مللي ثانية)',
        reconnectMaxDelay: 'أقصى تأخير لإعادة الاتصال (مللي ثانية)'
      }
    },
    createSuccess: 'تم إنشاء الواجهة "{name}".',
    updateSuccess: 'تم تحديث الواجهة "{name}".',
    deleteSuccess: 'تم حذف الواجهة "{name}".'
  } as const;
