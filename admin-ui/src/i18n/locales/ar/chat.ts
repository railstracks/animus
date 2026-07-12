export const chat = {
    sessionLabel: 'جلسة',
    newSession: 'جلسة جديدة',
    refresh: 'تحديث',
    stopGenerating: 'توقف عن التوليد',
    emptyTitle: 'ابدأ محادثة',
    emptyDescription: 'اسأل Animus عن الذاكرة أو التكوين أو الجلسة المباشرة للبدء.',
    streamState: {
      streaming: 'البث...',
      stopped: 'توقف'
    },
    jumpToLatest: 'انتقل إلى الأحدث',
    composerPlaceholder: 'أرسل رسالة إلى أنيموس...',
    adminTokenLabel: 'رمز المشرف (اختياري)',
    send: 'أرسل',
    contextTitle: 'السياق',
    context: {
      session: 'جلسة',
      layers: 'طبقات',
      tools: 'أدوات',
      budget: 'الميزانية',
      fallbackNote: 'يتم تحديث لقطة السياق مع تغير الجلسات.'
    },
    sidebarTabs: {
      context: 'السياق',
      sessions: 'الجلسات',
      messages: 'الرسائل',
      noSessions: 'لا توجد جلسات متاحة بعد.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'قم بإنشاء رسالة لبدء موضوع جديد.'
    },
    thinking: {
      label: "التفكير"
    },
    toolResult: {
      label: "نتيجة الأداة"
    },
    toolCall: {
      label: "نداء الأداة"
    },
    reasoning: {
      label: 'وضع الاستدلال',
      enabled: 'على',
      disabled: 'إيقاف',
      effort: 'جهد',
      instructionLabel: 'تعليمات النظام',
      instructionPlaceholder: 'تعليمات المنطق المخصصة (تستخدم الافتراضي إذا كانت فارغة)',
      notSupported: 'المنطق غير متوفر لهذا النموذج.'
    },
    provider: {
      label: 'مزود',
      select: 'مزود',
      model: 'نموذج'
    },
    agent: {
      label: 'الوكيل (جلسة جديدة)'
    },
    status: {
      closed: 'مغلق',
      connecting: 'الاتصال',
      open: 'متصل'
    },
    errors: {
      websocket: 'خطأ ويب سوكيت',
      websocketNotConnected: 'WebSocket غير متصل بعد. يرجى المحاولة مرة أخرى.',
      agentNotReady: 'لا يزال اختيار الوكيل قيد التحميل. الرجاء الانتظار لحظة وحاول مرة أخرى.',
      unknownWebsocket: 'خطأ غير معروف في مقبس الويب',
      sessionLoadFailed: 'فشل تحميل الجلسة {sessionId}: {message}'
    }
  } as const;
