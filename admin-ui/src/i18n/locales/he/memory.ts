export const memory = {
    title: 'דפדפן זיכרון',
    actions: {
      refresh: 'רענן',
      triggerConsolidation: 'הפעל איחוד'
    },
    common: {
      notAvailable: 'לא'
    },
    layers: {
      title: 'שכבות',
      empty: 'לא נמצאו שכבות זיכרון.',
      horizon: 'אופק',
      consolidationInterval: 'מרווח איחוד',
      retentionPolicy: 'מדיניות שימור',
      perspectiveCurrent: 'פרספקטיבה נוכחית',
      perspectivePast: 'נקודת מבט עבר',
      perspectiveFuture: 'פרספקטיבה עתידית',
      viewObservations: 'צפה בתצפיות'
    },
    consolidation: {
      title: 'איחוד',
      activeJob: 'עבודה פעילה',
      lastRun: 'ריצה אחרונה',
      lastJob: 'עבודה אחרונה',
      promoted: 'מקודם',
      demoted: 'הורד בדרגה',
      archived: 'בארכיון',
      state: {
        running: 'ריצה',
        idle: 'בטלה'
      }
    },
    list: {
      title: 'תצפיות',
      activeLayer: 'שכבה: {layer}',
      sortBy: 'מיון',
      orderBy: 'סדר',
      tagFilter: 'סינון לפי תגים',
      pageSize: 'גודל עמוד',
      compactMode: 'קומפקטי',
      openDetail: 'פרטים',
      emptyLayer: 'אין עדיין תצפיות בשכבה זו.',
      emptyFilter: 'אין תצפיות שתואמות את מסנן התג הנוכחי.',
      sort: {
        weight: 'משקל',
        age: 'גיל',
        tags: 'תגים'
      },
      order: {
        desc: 'יורד',
        asc: 'עולה'
      },
      columns: {
        content: 'תוכן',
        tags: 'תגים',
        weight: 'משקל',
        age: 'גיל',
        decay: 'ריקבון',
        actions: 'פעולות'
      }
    },
    detail: {
      title: 'פרט תצפית',
      empty: 'בחר תצפית לבדיקה ולעריכה.',
      id: 'תעודה מזהה',
      content: 'תוכן',
      layer: 'שכבה',

      timestamp: 'חותמת זמן',
      demotionReason: 'סיבה להורדה בדרגה',
      demotionTimestamp: 'חותמת זמן להורדה',
      editWeight: 'משקל',
      editTags: 'תגים',
      save: 'שמור',
      reset: 'אפס',
      archive: 'ארכיון',
      saveSuccess: 'התצפית עודכנה.',
      archiveSuccess: 'התצפית הועברה לארכיון.'
    }
  } as const;
