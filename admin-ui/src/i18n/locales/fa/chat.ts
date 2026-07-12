export const chat = {
    sessionLabel: 'جلسه',
    newSession: 'جلسه جدید',
    refresh: 'تازه کردن',
    stopGenerating: 'توقف تولید',
    emptyTitle: 'یک مکالمه را شروع کنید',
    emptyDescription: 'از Animus در مورد حافظه، پیکربندی یا یک جلسه زنده بپرسید.',
    streamState: {
      streaming: 'جریان ...',
      stopped: 'متوقف شد'
    },
    jumpToLatest: 'پرش به آخرین',
    composerPlaceholder: 'ارسال پیام به انیموس...',
    adminTokenLabel: 'رمز مدیریت (اختیاری)',
    send: 'ارسال کنید',
    contextTitle: 'زمینه',
    context: {
      session: 'جلسه',
      layers: 'لایه ها',
      tools: 'ابزار',
      budget: 'بودجه',
      fallbackNote: 'با تغییر جلسات، عکس متنی به‌روزرسانی می‌شود.'
    },
    sidebarTabs: {
      context: 'زمینه',
      sessions: 'جلسات',
      messages: 'پیام ها',
      noSessions: 'هنوز هیچ جلسه ای در دسترس نیست.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'برای شروع یک موضوع جدید پیامی بنویسید.'
    },
    thinking: {
      label: "فکر کردن"
    },
    toolResult: {
      label: "نتیجه ابزار"
    },
    toolCall: {
      label: "تماس ابزار"
    },
    reasoning: {
      label: 'حالت استدلال',
      enabled: 'روشن',
      disabled: 'خاموش',
      effort: 'تلاش',
      instructionLabel: 'دستورالعمل سیستم',
      instructionPlaceholder: 'دستورالعمل استدلال سفارشی (اگر خالی باشد از پیش فرض استفاده می کند)',
      notSupported: 'استدلال برای این مدل در دسترس نیست.'
    },
    provider: {
      label: 'ارائه دهنده',
      select: 'ارائه دهنده',
      model: 'مدل'
    },
    agent: {
      label: 'نماینده (جلسه جدید)'
    },
    status: {
      closed: 'بسته شد',
      connecting: 'اتصال',
      open: 'متصل شد'
    },
    errors: {
      websocket: 'خطای WebSocket',
      websocketNotConnected: 'WebSocket هنوز وصل نشده است. لطفا دوباره امتحان کنید.',
      agentNotReady: 'انتخاب نماینده هنوز در حال بارگیری است. لطفاً یک لحظه صبر کنید و دوباره امتحان کنید.',
      unknownWebsocket: 'خطای وب سوکت ناشناخته',
      sessionLoadFailed: 'جلسه بارگیری نشد {sessionId}: {message}'
    }
  } as const;
