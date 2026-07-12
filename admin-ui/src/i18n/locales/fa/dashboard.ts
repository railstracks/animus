export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'به انیموس خوش آمدید.',
    welcomeSub: 'اولین نماینده خود را برای شروع راه اندازی کنید.',
    beginSetup: 'شروع به تنظیم →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'آپتایم',
    agents: 'عوامل',
    sessions: 'جلسات',
    providers: 'ارائه دهندگان',
    errors: 'اشتباه کن',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'جلسات اخیر',
    viewAll: 'مشاهده همه',
    empty: 'هنوز جلسه ای وجود ندارد.',
    startChat: 'شروع یک چت →',
    messages: 'پیام',
  },
  // Memory card
  memory: {
    title: 'حافظه',
    inspect: 'بازرسی کنید',
    empty: 'هیچ لایه حافظه پیکربندی نشده است.',
    totalObservations: 'کل مشاهدات',
  },
  // Provider card
  providers: {
    title: 'ارائه دهندگان',
    manage: 'مدیریت کنید',
    empty: 'هیچ ارائه دهنده ای پیکربندی نشده است.',
    runWizard: 'جادوگر راه اندازی → را اجرا کنید',
  },
  // Quick Actions card
  quickActions: {
    title: 'اقدامات سریع',
    newChat: 'چت جدید',
    addAgent: 'افزودن نماینده',
    provider: 'ارائه دهنده',
    logs: 'سیاههها',
    scheduler: 'برنامه ریز',
    memory: 'حافظه',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'اطلاعات شبح',
    status: 'وضعیت',
    uptime: 'آپتایم',
    agents: 'عوامل',
    activeSessions: 'جلسات فعال',
    providers: 'ارائه دهندگان',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'لایه های حافظه',
    empty: 'هیچ لایه حافظه پیکربندی نشده است.',
  },
  // State labels
  state: {
    loading: 'در حال بارگذاری',
    unknown: 'ناشناخته',
  },
  // Error messages
  errors: {
    sessions: 'جلسات بارگیری نشد',
    providers: 'ارائه دهندگان بارگیری نشدند',
    memory: 'API حافظه در دسترس نیست',
  },
} as const;