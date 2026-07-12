export const memory = {
    title: 'Memory Browser',
    actions: {
      refresh: 'Refresh',
      triggerConsolidation: 'Run Consolidation'
    },
    common: {
      notAvailable: 'n/a'
    },
    layers: {
      title: 'Layers',
      empty: 'No memory layers found.',
      horizon: 'Horizon',
      consolidationInterval: 'Consolidation Interval',
      retentionPolicy: 'Retention Policy',
      perspectiveCurrent: 'Current Perspective',
      perspectivePast: 'Past Perspective',
      perspectiveFuture: 'Future Perspective',
      viewObservations: 'View Observations'
    },
    consolidation: {
      title: 'Consolidation',
      activeJob: 'Active Job',
      lastRun: 'Last Run',
      lastJob: 'Last Job',
      promoted: 'Promoted',
      demoted: 'Demoted',
      archived: 'Archived',
      state: {
        running: 'Running',
        idle: 'Idle'
      }
    },
    list: {
      title: 'Observations',
      activeLayer: 'Layer: {layer}',
      sortBy: 'Sort',
      orderBy: 'Order',
      tagFilter: 'Filter by Tags',
      pageSize: 'Page Size',
      compactMode: 'Compact',
      openDetail: 'Details',
      emptyLayer: 'No observations in this layer yet.',
      emptyFilter: 'No observations match the current tag filter.',
      sort: {
        weight: 'Weight',
        age: 'Age',
        tags: 'Tags'
      },
      order: {
        desc: 'Descending',
        asc: 'Ascending'
      },
      columns: {
        content: 'Content',
        tags: 'Tags',
        weight: 'Weight',
        age: 'Age',
        decay: 'Decay',
        actions: 'Actions'
      }
    },
    detail: {
      title: 'Observation Detail',
      empty: 'Select an observation to inspect and edit.',
      id: 'ID',
      content: 'Content',
      layer: 'Layer',

      timestamp: 'Timestamp',
      demotionReason: 'Demotion Reason',
      demotionTimestamp: 'Demotion Timestamp',
      editWeight: 'Weight',
      editTags: 'Tags',
      save: 'Save',
      reset: 'Reset',
      archive: 'Archive',
      saveSuccess: 'Observation updated.',
      archiveSuccess: 'Observation archived.'
    }
  } as const;
