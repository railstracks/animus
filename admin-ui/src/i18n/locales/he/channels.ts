export const channels = {
    title: 'ערוצים',
    empty: 'לא הוגדרו ערוצים. הוסף אחד כדי להתחיל.',
    createSuccess: 'הערוץ "{name}" נוצר בהצלחה.',
    updateSuccess: 'הערוץ "{name}" עודכן בהצלחה.',
    deleteSuccess: 'הערוץ "{name}" נמחק.',
    columns: {
      name: 'שם',
      type: 'הקלד',
      identity: 'זהות',
      endpoint: 'נקודת קצה',
      enabled: 'מופעל',
      connected: 'מחובר',
      lastEvent: 'אירוע אחרון',
      actions: 'פעולות'
    },
    state: {
      connected: 'מחובר',
      disconnected: 'מנותק'
    },
    actions: {
      refresh: 'רענן',
      add: 'הוסף ערוץ',
      cancel: 'בטל',
      create: 'צור',
      save: 'שמור',
      confirmDelete: 'האם למחוק את הערוץ "{name}"? לא ניתן לבטל זאת.'
    },
    form: {
      createTitle: 'הוסף ערוץ',
      editTitle: 'ערוך את "{name}"',
      name: 'שם הערוץ',
      type: 'סוג ערוץ',
      agent: 'סוכן',
      minResponseInterval: 'מרווח תגובה מינימלי (שניות)',
      allowInterjection: 'אפשר התערבות',
      irc: {
        host: 'שרת IRC',
        port: 'נמל',
        serverPassword: 'סיסמת שרת',
        nick: 'כינוי',
        username: 'שם משתמש',
        realname: 'שם אמיתי',
        channels: 'ערוצים',
        channelsHint: 'אחד בכל שורה. פורמט: #channel [מפתח]',
        agent: 'סוכן',
        respondToChannel: 'השב להודעות הערוץ',
        respondToDm: 'השב להודעות ישירות',
        respondToNotices: 'הגיבו להודעות',
        allowedDmUsers: 'משתמשי DM מותרים',
        reconnect: 'חיבור אוטומטי'
      },
      telegram: {
        botToken: 'Bot Token',
        tokenHint: 'השאר ריק כדי לשמור על האסימון הקיים'
      },
      vk: {
        accessToken: 'אסימון גישה לקהילה',
        groupId: 'מזהה קבוצה'
      },
      bluesky: {
        handle: 'ידית',
        appPassword: 'סיסמת אפליקציה',
        pds: 'כתובת אתר PDS'
      },
      mastodon: {
        handle: 'ידית',
        instance: 'כתובת אתר של מופע'
      },
      email: {
        apiKey: 'מפתח API של AgentMail',
        apiKeyHint: 'השאר ריק כדי לשמור על המפתח הנוכחי',
        inboxId: 'מזהה תיבת דואר נכנס',
        pollingWait: 'מרווח סקר (שניות)'
      },
      twitter: {
        tier: 'שכבת API',
        clientId: 'מזהה לקוח OAuth',
        clientSecret: 'סוד לקוח OAuth',
        accessToken: 'אסימון גישה',
        tokenHint: 'השאר ריק כדי לשמור על האסימון הקיים',
        refreshToken: 'רענן אסימון',
        authorize: 'הרשאה באמצעות טוויטר',
        oauthStarted: 'נפתח חלון ההרשאה. השלם את הזרימה בדפדפן שלך.'
      },
      discord: {
        botToken: 'Bot Token',
        tokenHint: 'השאר ריק כדי לשמור על האסימון הקיים',
        applicationId: 'מזהה יישום (לפקודות לוכסן)',
        monitoredChannels: 'ערוצים מנוטרים',
        monitoredChannelsHint: 'מזהה ערוץ אחד בכל שורה. הבוט חייב להיות בשרת.',
        respondToDm: 'הגיבו להודעות DM',
        respondToMentions: 'הגיבו לאזכורים',
        dmWhitelistEnabled: 'הגבלת הודעות ישירות למשתמשים מורשים',
        allowedDmUsers: 'משתמשי הודעות ישירות מורשים',
        allowedDmUsersHint: 'שמות משתמשים ב-Discord (אחד בכל שורה). רק משתמשים אלה יכולים לשלוח הודעות ישירות לבוט.'
      },
      slack: {
        botToken: 'Bot Token (xoxb-)',
        tokenHint: 'השאר ריק כדי לשמור על האסימון הקיים',
        appToken: 'אסימון אפליקציה (xapp-)',
        appTokenHint: 'עבור מצב Socket (שלב 2). אופציונלי עבור שלב 1.',
        monitoredChannels: 'ערוצים מנוטרים (עקוף)',
        monitoredChannelsHint: 'הבוט מפקח אוטומטית על כל הערוצים שהוא חבר בהם. הוסף רק מזהים כאן כדי להגביל את ההיקף.',
        respondToMentions: "השב לאזכורים {'@'}",
        respondToAll: 'השב לכל ההודעות (מתעלם מסנן אזכור)',
        threadedReplies: 'תשובות לשרשור בערוצים (כל הודעה פותחת שרשור)'
      }
    }
  } as const;
