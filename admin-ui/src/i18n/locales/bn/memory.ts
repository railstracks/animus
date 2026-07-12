export const memory = {
    title: 'মেমরি ব্রাউজার',
    actions: {
      refresh: 'রিফ্রেশ',
      triggerConsolidation: 'একত্রীকরণ চালান'
    },
    common: {
      notAvailable: 'n/a'
    },
    layers: {
      title: 'স্তর',
      empty: 'কোন মেমরি স্তর খুঁজে পাওয়া যায়নি.',
      horizon: 'দিগন্ত',
      consolidationInterval: 'একত্রীকরণ ব্যবধান',
      retentionPolicy: 'ধরে রাখার নীতি',
      perspectiveCurrent: 'বর্তমান প্রেক্ষিত',
      perspectivePast: 'অতীত পরিপ্রেক্ষিত',
      perspectiveFuture: 'ভবিষ্যত প্রেক্ষিত',
      viewObservations: 'পর্যবেক্ষণ দেখুন'
    },
    consolidation: {
      title: 'একত্রীকরণ',
      activeJob: 'সক্রিয় কাজ',
      lastRun: 'শেষ রান',
      lastJob: 'শেষ কাজ',
      promoted: 'পদোন্নতি',
      demoted: 'অবনমন',
      archived: 'সংরক্ষণাগারভুক্ত',
      state: {
        running: 'চলমান',
        idle: 'নিষ্ক্রিয়'
      }
    },
    list: {
      title: 'পর্যবেক্ষণ',
      activeLayer: 'স্তর: {layer}',
      sortBy: 'সাজান',
      orderBy: 'অর্ডার',
      tagFilter: 'ট্যাগ দ্বারা ফিল্টার',
      pageSize: 'পৃষ্ঠার আকার',
      compactMode: 'কমপ্যাক্ট',
      openDetail: 'বিস্তারিত',
      emptyLayer: 'এই স্তরে এখনো কোনো পর্যবেক্ষণ নেই।',
      emptyFilter: 'বর্তমান ট্যাগ ফিল্টারের সাথে কোনো পর্যবেক্ষণ মেলে না।',
      sort: {
        weight: 'ওজন',
        age: 'বয়স',
        tags: 'ট্যাগ'
      },
      order: {
        desc: 'অবরোহী',
        asc: 'আরোহী'
      },
      columns: {
        content: 'বিষয়বস্তু',
        tags: 'ট্যাগ',
        weight: 'ওজন',
        age: 'বয়স',
        decay: 'ক্ষয়',
        actions: 'কর্ম'
      }
    },
    detail: {
      title: 'পর্যবেক্ষণ বিস্তারিত',
      empty: 'পরিদর্শন এবং সম্পাদনা করার জন্য একটি পর্যবেক্ষণ নির্বাচন করুন।',
      id: 'আইডি',
      content: 'বিষয়বস্তু',
      layer: 'স্তর',

      timestamp: 'টাইমস্ট্যাম্প',
      demotionReason: 'ডিমোশনের কারণ',
      demotionTimestamp: 'ডেমোশন টাইমস্ট্যাম্প',
      editWeight: 'ওজন',
      editTags: 'ট্যাগ',
      save: 'সংরক্ষণ করুন',
      reset: 'রিসেট করুন',
      archive: 'সংরক্ষণাগার',
      saveSuccess: 'পর্যবেক্ষণ আপডেট করা হয়েছে।',
      archiveSuccess: 'পর্যবেক্ষণ সংরক্ষণাগারভুক্ত.'
    }
  } as const;
