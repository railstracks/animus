export const memory = {
    title: 'مرورگر حافظه',
    actions: {
      refresh: 'تازه کردن',
      triggerConsolidation: 'Consolidation را اجرا کنید'
    },
    common: {
      notAvailable: 'n/a'
    },
    layers: {
      title: 'لایه ها',
      empty: 'هیچ لایه حافظه ای یافت نشد.',
      horizon: 'افق',
      consolidationInterval: 'فاصله تثبیت',
      retentionPolicy: 'سیاست حفظ',
      perspectiveCurrent: 'دیدگاه فعلی',
      perspectivePast: 'دیدگاه گذشته',
      perspectiveFuture: 'چشم انداز آینده',
      viewObservations: 'مشاهده مشاهدات'
    },
    consolidation: {
      title: 'تحکیم',
      activeJob: 'شغل فعال',
      lastRun: 'آخرین اجرا',
      lastJob: 'آخرین کار',
      promoted: 'ارتقاء یافت',
      demoted: 'تنزل یافت',
      archived: 'بایگانی شد',
      state: {
        running: 'در حال دویدن',
        idle: 'بیکار'
      }
    },
    list: {
      title: 'مشاهدات',
      activeLayer: 'لایه: {layer}',
      sortBy: 'مرتب کردن',
      orderBy: 'سفارش دهید',
      tagFilter: 'فیلتر بر اساس برچسب',
      pageSize: 'اندازه صفحه',
      compactMode: 'فشرده',
      openDetail: 'جزئیات',
      emptyLayer: 'هنوز مشاهداتی در این لایه وجود ندارد.',
      emptyFilter: 'هیچ مشاهده ای با فیلتر برچسب فعلی مطابقت ندارد.',
      sort: {
        weight: 'وزن',
        age: 'سن',
        tags: 'برچسب ها'
      },
      order: {
        desc: 'نزولی',
        asc: 'صعودی'
      },
      columns: {
        content: 'محتوا',
        tags: 'برچسب ها',
        weight: 'وزن',
        age: 'سن',
        decay: 'پوسیدگی',
        actions: 'اقدامات'
      }
    },
    detail: {
      title: 'جزئیات مشاهده',
      empty: 'یک مشاهده را برای بازرسی و ویرایش انتخاب کنید.',
      id: 'شناسه',
      content: 'محتوا',
      layer: 'لایه',

      timestamp: 'مهر زمان',
      demotionReason: 'دلیل تنزل دادن',
      demotionTimestamp: 'مُهر زمان تنزل',
      editWeight: 'وزن',
      editTags: 'برچسب ها',
      save: 'ذخیره کنید',
      reset: 'بازنشانی کنید',
      archive: 'آرشیو',
      saveSuccess: 'مشاهده به روز شد.',
      archiveSuccess: 'مشاهده بایگانی شد.'
    }
  } as const;
