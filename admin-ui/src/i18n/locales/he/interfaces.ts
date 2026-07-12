export const interfaces = {
    title: 'ניהול ממשקים',
    actions: {
      refresh: 'רענון',
      add: 'הוסף ממשק',
      edit: 'עריכה',
      enable: 'הפעל',
      disable: 'השבת',
      delete: 'מחיקה',
      confirmDelete: 'למחוק את הממשק "{name}"?'
    },
    columns: {
      name: 'שם',
      type: 'סוג',
      module: 'מזהה מודול',
      enabled: 'מופעל',
      connected: 'מחובר',
      lastEvent: 'אירוע אחרון',
      actions: 'פעולות'
    },
    state: {
      enabled: 'כן',
      disabled: 'לא',
      connected: 'מחובר',
      disconnected: 'מנותק'
    },
    empty: 'עדיין לא הוגדרו ממשקים.',
    form: {
      createTitle: 'צור ממשק',
      editTitle: 'ערוך ממשק: {name}',
      name: 'שם מופע',
      type: 'סוג ממשק',
      moduleId: 'מזהה מודול',
      enabled: 'מופעל',
      configJson: 'JSON תצורה',
      create: 'צור',
      save: 'שמור',
      cancel: 'ביטול',
      irc: {
        host: 'מארח',
        port: 'פורט',
        nick: 'כינוי',
        serverPassword: 'סיסמת שרת',
        username: 'שם משתמש',
        realname: 'שם אמיתי',
        dmOnly: 'מצב הודעות פרטיות בלבד',
        respondToChannel: 'השב לפעילות בערוץ',
        respondToDm: 'השב להודעות ישירות',
        respondToNotices: 'השב להודעות מערכת',
        allowedDmUsers: 'משתמשי DM מורשים',
        allowedDmUsersHint: 'רשימת הרשאה אופציונלית; מופרדת בפסיקים או בשורות חדשות.',
        agentId: 'מזהה סוכן',
        reconnectEnabled: 'חיבור מחדש מופעל',
        reconnectInitialDelay: 'השהיית חיבור מחדש ראשונית (מ״ש)',
        reconnectMaxDelay: 'השהיית חיבור מחדש מרבית (מ״ש)'
      }
    },
    createSuccess: 'הממשק "{name}" נוצר.',
    updateSuccess: 'הממשק "{name}" עודכן.',
    deleteSuccess: 'הממשק "{name}" נמחק.'
  } as const;
