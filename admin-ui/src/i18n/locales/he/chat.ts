export const chat = {
    sessionLabel: 'מושב',
    newSession: 'מושב חדש',
    refresh: 'רענן',
    stopGenerating: 'תפסיק לייצר',
    emptyTitle: 'התחל שיחה',
    emptyDescription: 'שאל את אנימוס על זיכרון, תצורה או סשן חי כדי להתחיל להתגלגל.',
    streamState: {
      streaming: 'סטרימינג...',
      stopped: 'נעצר'
    },
    jumpToLatest: 'קפוץ לגרסה האחרונה',
    composerPlaceholder: 'שלח הודעה לאנימוס...',
    adminTokenLabel: 'אסימון מנהל מערכת (אופציונלי)',
    send: 'שלח',
    contextTitle: 'הקשר',
    context: {
      session: 'מושב',
      layers: 'שכבות',
      tools: 'כלים',
      budget: 'תקציב',
      fallbackNote: 'עדכוני תמונת הקשר עם שינויים בהפעלות.'
    },
    sidebarTabs: {
      context: 'הקשר',
      sessions: 'הפעלות',
      messages: 'הודעות',
      noSessions: 'אין עדיין הפעלות זמינות.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'כתוב הודעה כדי לפתוח שרשור חדש.'
    },
    thinking: {
      label: "חושבים"
    },
    toolResult: {
      label: "תוצאה של כלי"
    },
    toolCall: {
      label: "שיחת כלי"
    },
    reasoning: {
      label: 'מצב נימוק',
      enabled: 'פועל',
      disabled: 'כבוי',
      effort: 'מאמץ',
      instructionLabel: 'הוראת מערכת',
      instructionPlaceholder: 'הוראת הנמקה מותאמת אישית (משתמשת ברירת מחדל אם ריקה)',
      notSupported: 'נימוק אינו זמין עבור דגם זה.'
    },
    provider: {
      label: 'ספק',
      select: 'ספק',
      model: 'דגם'
    },
    agent: {
      label: 'סוכן (הפעלה חדשה)'
    },
    status: {
      closed: 'סגור',
      connecting: 'מתחבר',
      open: 'מחובר'
    },
    errors: {
      websocket: 'שגיאת WebSocket',
      websocketNotConnected: 'WebSocket עדיין לא מחובר. אנא נסה שוב.',
      agentNotReady: 'בחירת הסוכן עדיין בטעינה. אנא המתן רגע ונסה שוב.',
      unknownWebsocket: 'שגיאה לא ידועה ב-websocket',
      sessionLoadFailed: 'טעינת ההפעלה {sessionId} נכשלה: {message}'
    }
  } as const;
