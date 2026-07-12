export const memory = {
    title: 'Mai Binciken Ƙwaƙwalwa',
    actions: {
      refresh: 'Sake sabuntawa',
      triggerConsolidation: 'Gudun Ƙarfafawa'
    },
    common: {
      notAvailable: 'n/a'
    },
    layers: {
      title: 'Yadudduka',
      empty: 'Ba a sami matakan ƙwaƙwalwar ajiya ba.',
      horizon: 'Horizon',
      consolidationInterval: 'Tazarar Haɗin Kai',
      retentionPolicy: 'Manufar Riƙewa',
      perspectiveCurrent: 'Halin Yanzu',
      perspectivePast: 'Halayen Baya',
      perspectiveFuture: 'Hangen gaba',
      viewObservations: 'Duba Abubuwan Lura'
    },
    consolidation: {
      title: 'Ƙarfafawa',
      activeJob: 'Aiki Mai Aiki',
      lastRun: 'Gudun Karshe',
      lastJob: 'Aiki na Karshe',
      promoted: 'An inganta',
      demoted: 'An rage',
      archived: 'Ajiye',
      state: {
        running: 'Gudu',
        idle: 'Rago'
      }
    },
    list: {
      title: 'Abun lura',
      activeLayer: 'Layer: {layer}',
      sortBy: 'Tsara',
      orderBy: 'Oda',
      tagFilter: 'Tace Tags',
      pageSize: 'Girman Shafin',
      compactMode: 'Karamin',
      openDetail: 'Cikakkun bayanai',
      emptyLayer: 'Babu abin dubawa a cikin wannan Layer tukuna.',
      emptyFilter: 'Babu abin lura da ya dace da tacewa na yanzu.',
      sort: {
        weight: 'Nauyi',
        age: 'Shekaru',
        tags: 'Tags'
      },
      order: {
        desc: 'Saukowa',
        asc: 'Hawan hawa'
      },
      columns: {
        content: 'Abun ciki',
        tags: 'Tags',
        weight: 'Nauyi',
        age: 'Shekaru',
        decay: 'Lalacewa',
        actions: 'Ayyuka'
      }
    },
    detail: {
      title: 'Cikakken Bayani',
      empty: 'Zaɓi abin dubawa don dubawa da shiryawa.',
      id: 'ID',
      content: 'Abun ciki',
      layer: 'Layer',

      timestamp: 'Tambarin lokaci',
      demotionReason: 'Dalilin Rasa',
      demotionTimestamp: 'Tambarin Kasawa',
      editWeight: 'Nauyi',
      editTags: 'Tags',
      save: 'Ajiye',
      reset: 'Sake saiti',
      archive: 'Taskoki',
      saveSuccess: 'An sabunta lura.',
      archiveSuccess: 'An adana abin lura.'
    }
  } as const;
