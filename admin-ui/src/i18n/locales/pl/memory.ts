export const memory = {
    title: 'Przeglądarka pamięci',
    actions: {
      refresh: 'Odśwież',
      triggerConsolidation: 'Uruchom konsolidację'
    },
    common: {
      notAvailable: 'nie dotyczy'
    },
    layers: {
      title: 'Warstwy',
      empty: 'Nie znaleziono warstw pamięci.',
      horizon: 'Horyzont',
      consolidationInterval: 'Interwał konsolidacji',
      retentionPolicy: 'Polityka przechowywania',
      perspectiveCurrent: 'Aktualna perspektywa',
      perspectivePast: 'Przeszła perspektywa',
      perspectiveFuture: 'Perspektywa przyszłości',
      viewObservations: 'Zobacz obserwacje'
    },
    consolidation: {
      title: 'Konsolidacja',
      activeJob: 'Aktywna praca',
      lastRun: 'Ostatni bieg',
      lastJob: 'Ostatnia praca',
      promoted: 'Promowane',
      demoted: 'Zdegradowany',
      archived: 'Zarchiwizowane',
      state: {
        running: 'Bieganie',
        idle: 'Bezczynny'
      }
    },
    list: {
      title: 'Obserwacje',
      activeLayer: 'Warstwa: {layer}',
      sortBy: 'Sortuj',
      orderBy: 'Zamów',
      tagFilter: 'Filtruj według tagów',
      pageSize: 'Rozmiar strony',
      compactMode: 'Kompaktowy',
      openDetail: 'Szczegóły',
      emptyLayer: 'Nie ma jeszcze żadnych obserwacji w tej warstwie.',
      emptyFilter: 'Żadne obserwacje nie pasują do bieżącego filtra tagów.',
      sort: {
        weight: 'Waga',
        age: 'Wiek',
        tags: 'Tagi'
      },
      order: {
        desc: 'Malejąco',
        asc: 'Rosnąco'
      },
      columns: {
        content: 'Treść',
        tags: 'Tagi',
        weight: 'Waga',
        age: 'Wiek',
        decay: 'Rozpad',
        actions: 'Działania'
      }
    },
    detail: {
      title: 'Szczegóły obserwacji',
      empty: 'Wybierz obserwację do sprawdzenia i edycji.',
      id: 'Identyfikator',
      content: 'Treść',
      layer: 'Warstwa',

      timestamp: 'Znacznik czasu',
      demotionReason: 'Powód degradacji',
      demotionTimestamp: 'Znacznik czasu degradacji',
      editWeight: 'Waga',
      editTags: 'Tagi',
      save: 'Zapisz',
      reset: 'Zresetuj',
      archive: 'Archiwum',
      saveSuccess: 'Obserwacja zaktualizowana.',
      archiveSuccess: 'Obserwacja zarchiwizowana.'
    }
  } as const;
