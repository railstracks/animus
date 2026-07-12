export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'مرحبا بكم في أنيموس.',
    welcomeSub: 'قم بإعداد وكيلك الأول للبدء.',
    beginSetup: 'ابدأ الإعداد →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'الجهوزية',
    agents: 'الوكلاء',
    sessions: 'الجلسات',
    providers: 'مقدمي الخدمات',
    errors: 'يخطئ',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'الجلسات الأخيرة',
    viewAll: 'عرض الكل',
    empty: 'لا توجد جلسات بعد.',
    startChat: 'ابدأ محادثة →',
    messages: 'رسائل',
  },
  // Memory card
  memory: {
    title: 'الذاكرة',
    inspect: 'فحص',
    empty: 'لم يتم تكوين طبقات الذاكرة.',
    totalObservations: 'إجمالي الملاحظات',
  },
  // Provider card
  providers: {
    title: 'مقدمي الخدمات',
    manage: 'إدارة',
    empty: 'لم يتم تكوين أي مقدمي خدمات.',
    runWizard: 'قم بتشغيل معالج الإعداد →',
  },
  // Quick Actions card
  quickActions: {
    title: 'إجراءات سريعة',
    newChat: 'دردشة جديدة',
    addAgent: 'إضافة وكيل',
    provider: 'مزود',
    logs: 'سجلات',
    scheduler: 'مجدول',
    memory: 'الذاكرة',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'معلومات الشيطان',
    status: 'الحالة',
    uptime: 'الجهوزية',
    agents: 'الوكلاء',
    activeSessions: 'جلسات نشطة',
    providers: 'مقدمي الخدمات',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'طبقات الذاكرة',
    empty: 'لم يتم تكوين طبقات الذاكرة.',
  },
  // State labels
  state: {
    loading: 'جاري التحميل',
    unknown: 'غير معروف',
  },
  // Error messages
  errors: {
    sessions: 'فشل تحميل الجلسات',
    providers: 'فشل في تحميل مقدمي الخدمة',
    memory: 'واجهة برمجة تطبيقات الذاكرة غير متاحة',
  },
} as const;