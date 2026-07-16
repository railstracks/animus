export const channels = {
    title: 'القنوات',
    empty: 'لم يتم تكوين أي قنوات. أضف واحدًا للبدء.',
    createSuccess: 'تم إنشاء القناة "{name}" بنجاح.',
    updateSuccess: 'تم تحديث القناة "{name}" بنجاح.',
    deleteSuccess: 'تم حذف القناة "{name}".',
    columns: {
      name: 'الاسم',
      type: 'اكتب',
      identity: 'الهوية',
      endpoint: 'نقطة النهاية',
      enabled: 'ممكّن',
      connected: 'متصل',
      lastEvent: 'الحدث الأخير',
      actions: 'الإجراءات'
    },
    state: {
      connected: 'متصل',
      disconnected: 'غير متصل'
    },
    actions: {
      refresh: 'تحديث',
      add: 'أضف قناة',
      cancel: 'إلغاء',
      create: 'إنشاء',
      save: 'حفظ',
      confirmDelete: 'هل تريد حذف القناة "{name}"؟ لا يمكن التراجع عن هذا.'
    },
    form: {
      createTitle: 'أضف قناة',
      editTitle: 'تحرير "{name}"',
      name: 'اسم القناة',
      type: 'نوع القناة',
      agent: 'وكيل',
      minResponseInterval: 'الحد الأدنى لفاصل الاستجابة (ثوانٍ)',
      allowInterjection: 'السماح بالتدخل',
      irc: {
        host: 'خادم آي آر سي',
        port: 'ميناء',
        serverPassword: 'كلمة مرور الخادم',
        nick: 'اللقب',
        username: 'اسم المستخدم',
        realname: 'الاسم الحقيقي',
        channels: 'القنوات',
        channelsHint: 'واحد لكل سطر. التنسيق: #channel [مفتاح]',
        agent: 'وكيل',
        respondToChannel: 'الرد على رسائل القناة',
        respondToDm: 'الرد على الرسائل المباشرة',
        respondToNotices: 'الرد على الإشعارات',
        allowedDmUsers: 'مسموح لمستخدمي DM',
        reconnect: 'إعادة الاتصال التلقائي'
      },
      telegram: {
        botToken: 'رمز بوت',
        tokenHint: 'اتركه فارغًا للاحتفاظ بالرمز الموجود'
      },
      vk: {
        accessToken: 'رمز وصول المجتمع',
        groupId: 'معرف المجموعة'
      },
      bluesky: {
        handle: 'مقبض',
        appPassword: 'كلمة مرور التطبيق',
        pds: 'عنوان URL لنظام التوزيع العام'
      },
      mastodon: {
        handle: 'مقبض',
        instance: 'عنوان URL للمثيل'
      },
      email: {
        apiKey: 'مفتاح واجهة برمجة تطبيقات AgentMail',
        apiKeyHint: 'اتركه فارغًا للاحتفاظ بالمفتاح الحالي',
        inboxId: 'معرف البريد الوارد',
        pollingWait: 'الفاصل الزمني للاستقصاء (بالثواني)'
      },
      twitter: {
        tier: 'طبقة واجهة برمجة التطبيقات',
        clientId: 'معرف عميل OAuth',
        clientSecret: 'سر عميل OAuth',
        accessToken: 'رمز الوصول',
        tokenHint: 'اتركه فارغًا للاحتفاظ بالرمز الموجود',
        refreshToken: 'تحديث الرمز المميز',
        authorize: 'التخويل مع تويتر',
        oauthStarted: 'تم فتح نافذة التفويض. أكمل التدفق في متصفحك.'
      },
      discord: {
        botToken: 'رمز بوت',
        tokenHint: 'اتركه فارغًا للاحتفاظ بالرمز الموجود',
        applicationId: 'معرف التطبيق (لأوامر الشرطة المائلة)',
        monitoredChannels: 'القنوات المراقبة',
        monitoredChannelsHint: 'معرف قناة واحد لكل سطر. يجب أن يكون بوت في الخادم.',
        respondToDm: 'الرد على الرسائل المباشرة',
        respondToMentions: 'الرد على الإشارات',
        dmWhitelistEnabled: 'قصر الرسائل المباشرة على المستخدمين المسموح لهم',
        allowedDmUsers: 'مستخدمو الرسائل المباشرة المسموح لهم',
        allowedDmUsersHint: 'أسماء مستخدمي Discord (واحد لكل سطر). يمكن لهؤلاء المستخدمين فقط إرسال رسائل مباشرة للبوت.'
      },
      slack: {
        botToken: 'رمز الروبوت (xoxb-)',
        tokenHint: 'اتركه فارغًا للاحتفاظ بالرمز الموجود',
        appToken: 'رمز التطبيق (xapp-)',
        appTokenHint: 'لوضع المقبس (المرحلة 2). اختياري للمرحلة الأولى.',
        monitoredChannels: 'القنوات المراقبة (تجاوز)',
        monitoredChannelsHint: 'يقوم الروبوت تلقائيًا بمراقبة جميع القنوات التي ينتمي إليها. أضف المعرفات هنا فقط لتقييد النطاق.',
        respondToMentions: "الرد على {'@'}الإشارات",
        respondToAll: 'الرد على جميع الرسائل (يتجاهل مرشح الإشارة)',
        threadedReplies: 'ردود الموضوع في القنوات (كل رسالة تبدأ سلسلة رسائل)'
      }
    }
  } as const;
