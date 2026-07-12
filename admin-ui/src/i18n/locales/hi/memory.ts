export const memory = {
    title: 'मेमोरी ब्राउज़र',
    actions: {
      refresh: 'ताज़ा करें',
      triggerConsolidation: 'समेकन चलाएँ'
    },
    common: {
      notAvailable: 'एन/ए'
    },
    layers: {
      title: 'परतें',
      empty: 'कोई स्मृति परतें नहीं मिलीं.',
      horizon: 'क्षितिज',
      consolidationInterval: 'समेकन अंतराल',
      retentionPolicy: 'प्रतिधारण नीति',
      perspectiveCurrent: 'वर्तमान परिप्रेक्ष्य',
      perspectivePast: 'विगत परिप्रेक्ष्य',
      perspectiveFuture: 'भविष्य का परिप्रेक्ष्य',
      viewObservations: 'अवलोकन देखें'
    },
    consolidation: {
      title: 'समेकन',
      activeJob: 'सक्रिय कार्य',
      lastRun: 'अंतिम रन',
      lastJob: 'आखिरी काम',
      promoted: 'प्रचारित',
      demoted: 'पदावनत',
      archived: 'संग्रहीत',
      state: {
        running: 'चल रहा है',
        idle: 'निष्क्रिय'
      }
    },
    list: {
      title: 'टिप्पणियाँ',
      activeLayer: 'परत: {layer}',
      sortBy: 'क्रमबद्ध करें',
      orderBy: 'आदेश',
      tagFilter: 'टैग द्वारा फ़िल्टर करें',
      pageSize: 'पृष्ठ का आकार',
      compactMode: 'सघन',
      openDetail: 'विवरण',
      emptyLayer: 'इस परत में अभी तक कोई अवलोकन नहीं है.',
      emptyFilter: 'कोई भी अवलोकन वर्तमान टैग फ़िल्टर से मेल नहीं खाता।',
      sort: {
        weight: 'वज़न',
        age: 'उम्र',
        tags: 'टैग'
      },
      order: {
        desc: 'उतरता हुआ',
        asc: 'आरोही'
      },
      columns: {
        content: 'सामग्री',
        tags: 'टैग',
        weight: 'वज़न',
        age: 'उम्र',
        decay: 'क्षय',
        actions: 'क्रियाएँ'
      }
    },
    detail: {
      title: 'अवलोकन विवरण',
      empty: 'निरीक्षण और संपादन के लिए एक अवलोकन का चयन करें।',
      id: 'आईडी',
      content: 'सामग्री',
      layer: 'परत',

      timestamp: 'टाइमस्टैम्प',
      demotionReason: 'पदावनति कारण',
      demotionTimestamp: 'डिमोशन टाइमस्टैम्प',
      editWeight: 'वज़न',
      editTags: 'टैग',
      save: 'सहेजें',
      reset: 'रीसेट करें',
      archive: 'पुरालेख',
      saveSuccess: 'अवलोकन अद्यतन किया गया.',
      archiveSuccess: 'अवलोकन संग्रहीत.'
    }
  } as const;
