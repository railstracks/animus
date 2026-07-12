export const memoryFiles = {
    title: 'קבצי זיכרון',
    subtitle: 'אחסן חפצי טקסט גולמיים וחפש בדומיינים של זיכרון.',
    common: {
      yes: 'כן',
      no: 'לא'
    },
    actions: {
      refresh: 'רענן',
      open: 'פתוח',
      delete: 'מחק',
      process: 'תור לעיבוד',
      import: 'ייבוא',
      importBatch: 'ייבוא אצווה',
      saveMetadata: 'שמור מטא נתונים',
      search: 'חפש'
    },
    fields: {
      sourcePath: 'נתיב מקור',
      fileType: 'סוג קובץ',
      contentMutable: 'תוכן ניתן לשינוי',
      agentId: 'מזהה סוכן',
      status: 'סטטוס',
      agentFilter: 'סנן לפי סוכן',
      allAgents: 'כל הסוכנים',
      superseded: 'הוחלף',
      content: 'תוכן'
    },
    types: {
      all: 'כל הסוגים',
      expanded_memory: 'זיכרון מורחב',
      session_log: 'יומן הפעלה',
      daily_note: 'הערה יומית',
      bootstrap_file: 'קובץ Bootstrap',
      journal: 'יומן',
      external_doc: 'מסמך חיצוני'
    },
    stats: {
      title: 'סטטיסטיקות'
    },
    list: {
      title: 'רשימת קבצים',
      typeFilter: 'הקלד מסנן',
      limit: 'הגבלה',
      offset: 'קיזוז',
      columns: {
        id: 'תעודה מזהה',
        type: 'הקלד',
        sourcePath: 'נתיב מקור',
        contentMutable: 'ניתן לשינוי',
        agentId: 'מזהה סוכן',
      status: 'סטטוס',
      agentFilter: 'סנן לפי סוכן',
      allAgents: 'כל הסוכנים',
        superseded: 'הוחלף',
        importedAt: 'מיובא',
        actions: 'פעולות'
      },
      status: {
        unprocessed: 'לא מעובד',
        processed: 'מעובד'
      }
    },
    import: {
      singleTitle: 'ייבוא קובץ',
      selectFile: 'בחר קובץ',
      selected: 'נבחר',
      batchTitle: 'ייבוא אצווה',
      selectFiles: 'בחר קבצים',
      filesSelected: 'קבצים שנבחרו'
    },
    detail: {
      title: 'פירוט הקובץ',
      empty: 'בחר קובץ מהרשימה כדי לבדוק ולערוך מטא נתונים.',
      createdAt: 'נוצר',
      importedAt: 'מיובא',
      contentTitle: 'תוכן',
      contentImmutableNotice: 'עריכת תוכן מושבתת מכיוון שקובץ זה מסומן כבלתי ניתן לשינוי.'
    },
    search: {
      title: 'חיפוש מאוחד',
      query: 'שאילתת חיפוש',
      limit: 'הגבלה',
      relevance: 'רלוונטיות',
      empty: 'אין עדיין תוצאות חיפוש.',
      domains: {
        observation: 'תצפית',
        observations: 'תצפיות',
        ontology: 'אונטולוגיה',
        memory_file: 'קבצים',
        sessions: 'הפעלות'
      }
    },
    errors: {
      loadFiles: 'טעינת קבצי זיכרון נכשלה.',
      loadDetail: 'טעינת פרטי הקובץ נכשלה.',
      importRequired: 'בחר קובץ לייבוא.',
      importSingle: 'ייבוא קובץ הזיכרון נכשל.',
      batchRequired: 'בחר לפחות קובץ אחד לייבוא אצווה.',
      importBatch: 'ייבוא מטען אצווה נכשל.',
      updateMetadata: 'עדכון המטא נתונים נכשל.',
      delete: 'מחיקת קובץ הזיכרון נכשלה.',
      search: 'חיפוש הזיכרון נכשל.'
    }
  } as const;
