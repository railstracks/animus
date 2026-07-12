export const activeMemory = {
    title: 'זיכרון פעיל',
    subtitle: 'הקשר של סוכן מורכב - מה שהסוכן רואה בהקדמה שלו',
    empty: 'בחר סוכן כדי להציג את ההקשר המורכב שלו.',
    actions: {
      refresh: 'רענן'
    },
    labels: {
      agent: 'סוכן',
      session: 'מושב',
      syntheticSession: 'סינתטי (מפגש ריק לבדיקה)',
      blocks: 'בלוקים'
    },
    flags: {
      synthetic: 'הפעלה סינתטית',
      live: 'סשן חי'
    },
    sections: {
      rendered: 'פלט מעובד',
      blocks: 'התמוטטות בלוקים'
    }
  } as const;
