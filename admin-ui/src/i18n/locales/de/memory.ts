export const memory = {
    title: 'Speicherbrowser',
    actions: {
      refresh: 'Aktualisieren',
      triggerConsolidation: 'Führen Sie die Konsolidierung aus'
    },
    common: {
      notAvailable: 'n/a'
    },
    layers: {
      title: 'Schichten',
      empty: 'Keine Speicherebenen gefunden.',
      horizon: 'Horizont',
      consolidationInterval: 'Konsolidierungsintervall',
      retentionPolicy: 'Aufbewahrungsrichtlinie',
      perspectiveCurrent: 'Aktuelle Perspektive',
      perspectivePast: 'Vergangene Perspektive',
      perspectiveFuture: 'Zukunftsperspektive',
      viewObservations: 'Beobachtungen anzeigen'
    },
    consolidation: {
      title: 'Konsolidierung',
      activeJob: 'Aktiver Job',
      lastRun: 'Letzter Lauf',
      lastJob: 'Letzter Job',
      promoted: 'Gefördert',
      demoted: 'Herabgestuft',
      archived: 'Archiviert',
      state: {
        running: 'Laufen',
        idle: 'Leerlauf'
      }
    },
    list: {
      title: 'Beobachtungen',
      activeLayer: 'Schicht: {layer}',
      sortBy: 'Sortieren',
      orderBy: 'Bestellen',
      tagFilter: 'Nach Tags filtern',
      pageSize: 'Seitengröße',
      compactMode: 'Kompakt',
      openDetail: 'Details',
      emptyLayer: 'Noch keine Beobachtungen in dieser Ebene.',
      emptyFilter: 'Keine Beobachtungen stimmen mit dem aktuellen Tag-Filter überein.',
      sort: {
        weight: 'Gewicht',
        age: 'Alter',
        tags: 'Schlagworte'
      },
      order: {
        desc: 'Absteigend',
        asc: 'Aufsteigend'
      },
      columns: {
        content: 'Inhalt',
        tags: 'Schlagworte',
        weight: 'Gewicht',
        age: 'Alter',
        decay: 'Verfall',
        actions: 'Aktionen'
      }
    },
    detail: {
      title: 'Beobachtungsdetail',
      empty: 'Wählen Sie eine Beobachtung zum Überprüfen und Bearbeiten aus.',
      id: 'Ausweis',
      content: 'Inhalt',
      layer: 'Schicht',

      timestamp: 'Zeitstempel',
      demotionReason: 'Grund für die Herabstufung',
      demotionTimestamp: 'Herabstufungszeitstempel',
      editWeight: 'Gewicht',
      editTags: 'Schlagworte',
      save: 'Speichern',
      reset: 'Zurücksetzen',
      archive: 'Archiv',
      saveSuccess: 'Beobachtung aktualisiert.',
      archiveSuccess: 'Beobachtung archiviert.'
    }
  } as const;
