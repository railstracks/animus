export const memory = {
    title: 'កម្មវិធីរុករកអង្គចងចាំ',
    actions: {
      refresh: 'ធ្វើឱ្យស្រស់',
      triggerConsolidation: 'ដំណើរការការបង្រួបបង្រួម'
    },
    common: {
      notAvailable: 'ន/ក'
    },
    layers: {
      title: 'ស្រទាប់',
      empty: 'រកមិនឃើញស្រទាប់អង្គចងចាំទេ។',
      horizon: 'Horizon',
      consolidationInterval: 'ចន្លោះពេលរួម',
      retentionPolicy: 'គោលការណ៍រក្សា',
      perspectiveCurrent: 'ទស្សនវិស័យបច្ចុប្បន្ន',
      perspectivePast: 'ទស្សនវិស័យអតីតកាល',
      perspectiveFuture: 'ទស្សនវិស័យអនាគត',
      viewObservations: 'មើលការសង្កេត'
    },
    consolidation: {
      title: 'ការបង្រួបបង្រួម',
      activeJob: 'ការងារសកម្ម',
      lastRun: 'ការរត់ចុងក្រោយ',
      lastJob: 'ការងារចុងក្រោយ',
      promoted: 'ផ្សព្វផ្សាយ',
      demoted: 'ទម្លាក់',
      archived: 'ទុកក្នុងប័ណ្ណសារ',
      state: {
        running: 'កំពុងរត់',
        idle: 'ទំនេរ'
      }
    },
    list: {
      title: 'ការសង្កេត',
      activeLayer: 'ស្រទាប់៖ {layer}',
      sortBy: 'តម្រៀប',
      orderBy: 'បញ្ជាទិញ',
      tagFilter: 'ត្រងតាមស្លាក',
      pageSize: 'ទំហំទំព័រ',
      compactMode: 'បង្រួម',
      openDetail: 'ព័ត៌មានលម្អិត',
      emptyLayer: 'មិនមានការសង្កេតនៅក្នុងស្រទាប់នេះនៅឡើយទេ។',
      emptyFilter: 'គ្មានការសង្កេតដែលត្រូវគ្នានឹងតម្រងស្លាកបច្ចុប្បន្នទេ។',
      sort: {
        weight: 'ទម្ងន់',
        age: 'អាយុ',
        tags: 'ស្លាក'
      },
      order: {
        desc: 'ចុះ',
        asc: 'ឡើង'
      },
      columns: {
        content: 'មាតិកា',
        tags: 'ស្លាក',
        weight: 'ទម្ងន់',
        age: 'អាយុ',
        decay: 'ពុកផុយ',
        actions: 'សកម្មភាព'
      }
    },
    detail: {
      title: 'ព័ត៌មានលម្អិតនៃការសង្កេត',
      empty: 'ជ្រើសរើសការសង្កេតមួយដើម្បីត្រួតពិនិត្យ និងកែសម្រួល។',
      id: 'លេខសម្គាល់',
      content: 'មាតិកា',
      layer: 'ស្រទាប់',

      timestamp: 'ត្រាពេលវេលា',
      demotionReason: 'ហេតុផលរំសាយ',
      demotionTimestamp: 'ត្រាពេលវេលារំសាយ',
      editWeight: 'ទម្ងន់',
      editTags: 'ស្លាក',
      save: 'រក្សាទុក',
      reset: 'កំណត់ឡើងវិញ',
      archive: 'បណ្ណសារ',
      saveSuccess: 'បានធ្វើបច្ចុប្បន្នភាពការសង្កេត។',
      archiveSuccess: 'បានរក្សាទុកការសង្កេត។'
    }
  } as const;
