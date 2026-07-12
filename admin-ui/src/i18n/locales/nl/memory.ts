export const memory = {
    title: 'Geheugenbrowser',
    actions: {
      refresh: 'Vernieuwen',
      triggerConsolidation: 'Consolidatie uitvoeren'
    },
    common: {
      notAvailable: 'n.v.t.'
    },
    layers: {
      title: 'Lagen',
      empty: 'Geen geheugenlagen gevonden.',
      horizon: 'Horizon',
      consolidationInterval: 'Consolidatie-interval',
      retentionPolicy: 'Bewaarbeleid',
      perspectiveCurrent: 'Huidig perspectief',
      perspectivePast: 'Verleden perspectief',
      perspectiveFuture: 'Toekomstig perspectief',
      viewObservations: 'Observaties bekijken'
    },
    consolidation: {
      title: 'Consolidatie',
      activeJob: 'Actieve taak',
      lastRun: 'Laatste uitvoering',
      lastJob: 'Laatste taak',
      promoted: 'Gepromoveerd',
      demoted: 'Gedegradeerd',
      archived: 'Gearchiveerd',
      state: {
        running: 'Actief',
        idle: 'Inactief'
      }
    },
    list: {
      title: 'Observaties',
      activeLayer: 'Laag: {layer}',
      sortBy: 'Sorteren',
      orderBy: 'Volgorde',
      tagFilter: 'Filteren op tags',
      pageSize: 'Paginagrootte',
      compactMode: 'Compact',
      openDetail: 'Details',
      emptyLayer: 'Nog geen observaties in deze laag.',
      emptyFilter: 'Geen observaties komen overeen met het huidige tagfilter.',
      sort: {
        weight: 'Gewicht',
        age: 'Leeftijd',
        tags: 'Tags'
      },
      order: {
        desc: 'Aflopend',
        asc: 'Oplopend'
      },
      columns: {
        content: 'Inhoud',
        tags: 'Tags',
        weight: 'Gewicht',
        age: 'Leeftijd',
        decay: 'Verval',
        actions: 'Acties'
      }
    },
    detail: {
      title: 'Observatiedetail',
      empty: 'Selecteer een observatie om te inspecteren en te bewerken.',
      id: 'ID',
      content: 'Inhoud',
      layer: 'Laag',

      timestamp: 'Tijdstempel',
      demotionReason: 'Reden voor degradatie',
      demotionTimestamp: 'Tijdstempel degradatie',
      editWeight: 'Gewicht',
      editTags: 'Tags',
      save: 'Opslaan',
      reset: 'Resetten',
      archive: 'Archiveren',
      saveSuccess: 'Observatie bijgewerkt.',
      archiveSuccess: 'Observatie gearchiveerd.'
    }
  } as const;
