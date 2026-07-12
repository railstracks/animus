export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'ברוכים הבאים לאנימוס.',
    welcomeSub: 'הגדר את הסוכן הראשון שלך כדי להתחיל.',
    beginSetup: 'התחל בהגדרה →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'זמן פעולה',
    agents: 'סוכנים',
    sessions: 'הפעלות',
    providers: 'ספקים',
    errors: 'טעות',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'הפעלות אחרונות',
    viewAll: 'הצג הכל',
    empty: 'עדיין אין הפעלות.',
    startChat: 'התחל צ\'אט →',
    messages: 'הודעות',
  },
  // Memory card
  memory: {
    title: 'זיכרון',
    inspect: 'בדוק',
    empty: 'לא הוגדרו שכבות זיכרון.',
    totalObservations: 'סך תצפיות',
  },
  // Provider card
  providers: {
    title: 'ספקים',
    manage: 'נהל',
    empty: 'לא הוגדרו ספקים.',
    runWizard: 'הפעל את אשף ההתקנה →',
  },
  // Quick Actions card
  quickActions: {
    title: 'פעולות מהירות',
    newChat: 'צ\'אט חדש',
    addAgent: 'הוסף סוכן',
    provider: 'ספק',
    logs: 'יומנים',
    scheduler: 'מתזמן',
    memory: 'זיכרון',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'מידע על דימון',
    status: 'סטטוס',
    uptime: 'זמן פעולה',
    agents: 'סוכנים',
    activeSessions: 'הפעלות פעילות',
    providers: 'ספקים',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'שכבות זיכרון',
    empty: 'לא הוגדרו שכבות זיכרון.',
  },
  // State labels
  state: {
    loading: 'טוען',
    unknown: 'לא ידוע',
  },
  // Error messages
  errors: {
    sessions: 'טעינת ההפעלות נכשלה',
    providers: 'טעינת הספקים נכשלה',
    memory: 'API של זיכרון לא זמין',
  },
} as const;