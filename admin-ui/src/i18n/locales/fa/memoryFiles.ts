export const memoryFiles = {
    title: 'فایل های حافظه',
    subtitle: 'مصنوعات متن خام را ذخیره کنید و در دامنه های حافظه جستجو کنید.',
    common: {
      yes: 'بله',
      no: 'نه'
    },
    actions: {
      refresh: 'تازه کردن',
      open: 'باز کنید',
      delete: 'حذف کنید',
      process: 'صف برای پردازش',
      import: 'واردات',
      importBatch: 'وارد کردن دسته',
      saveMetadata: 'ذخیره متادیتا',
      search: 'جستجو کنید'
    },
    fields: {
      sourcePath: 'مسیر منبع',
      fileType: 'نوع فایل',
      contentMutable: 'محتوا قابل تغییر است',
      agentId: 'شناسه نماینده',
      status: 'وضعیت',
      agentFilter: 'فیلتر بر اساس نماینده',
      allAgents: 'همه نمایندگان',
      superseded: 'جایگزین شد',
      content: 'محتوا'
    },
    types: {
      all: 'همه انواع',
      expanded_memory: 'حافظه گسترش یافته',
      session_log: 'گزارش جلسه',
      daily_note: 'یادداشت روزانه',
      bootstrap_file: 'فایل بوت استرپ',
      journal: 'مجله',
      external_doc: 'سند خارجی'
    },
    stats: {
      title: 'آمار'
    },
    list: {
      title: 'لیست فایل',
      typeFilter: 'فیلتر را تایپ کنید',
      limit: 'محدود کنید',
      offset: 'افست',
      columns: {
        id: 'شناسه',
        type: 'تایپ کنید',
        sourcePath: 'مسیر منبع',
        contentMutable: 'قابل تغییر است',
        agentId: 'شناسه نماینده',
      status: 'وضعیت',
      agentFilter: 'فیلتر بر اساس نماینده',
      allAgents: 'همه نمایندگان',
        superseded: 'جایگزین شد',
        importedAt: 'وارداتی',
        actions: 'اقدامات'
      },
      status: {
        unprocessed: 'پردازش نشده',
        processed: 'پردازش شده است'
      }
    },
    import: {
      singleTitle: 'وارد کردن فایل',
      selectFile: 'فایل را انتخاب کنید',
      selected: 'انتخاب شده است',
      batchTitle: 'واردات دسته ای',
      selectFiles: 'فایل ها را انتخاب کنید',
      filesSelected: 'فایل های انتخاب شده'
    },
    detail: {
      title: 'جزئیات فایل',
      empty: 'فایلی را از لیست برای بررسی و ویرایش متادیتا انتخاب کنید.',
      createdAt: 'ایجاد شد',
      importedAt: 'وارداتی',
      contentTitle: 'محتوا',
      contentImmutableNotice: 'ویرایش محتوا غیرفعال است زیرا این فایل غیرقابل تغییر علامت گذاری شده است.'
    },
    search: {
      title: 'جستجوی یکپارچه',
      query: 'پرس و جو جستجو',
      limit: 'محدود کنید',
      relevance: 'ارتباط',
      empty: 'هنوز هیچ نتیجه جستجویی وجود ندارد.',
      domains: {
        observation: 'مشاهده',
        observations: 'مشاهدات',
        ontology: 'هستی شناسی',
        memory_file: 'فایل ها',
        sessions: 'جلسات'
      }
    },
    errors: {
      loadFiles: 'فایل های حافظه بارگیری نشد.',
      loadDetail: 'جزئیات فایل بارگیری نشد.',
      importRequired: 'فایلی را برای وارد کردن انتخاب کنید.',
      importSingle: 'وارد کردن فایل حافظه انجام نشد.',
      batchRequired: 'حداقل یک فایل را برای وارد کردن دسته ای انتخاب کنید.',
      importBatch: 'بار دسته ای وارد نشد.',
      updateMetadata: 'به روز رسانی متادیتا ناموفق بود.',
      delete: 'فایل حافظه حذف نشد.',
      search: 'جستجوی حافظه انجام نشد.'
    }
  } as const;
