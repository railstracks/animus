export const ontology = {
    title: 'کاوشگر هستی شناسی',
    subtitle: 'موجودیت‌های معنایی و ویژگی‌های مبتنی بر مشاهدات آنها را مرور کنید.',
    actions: {
      refresh: 'تازه کردن'
    },
    sections: {
      agent: 'عامل',
      categories: 'دسته بندی ها',
      entities: 'نهادها',
      details: 'جزئیات موجودیت',
      properties: 'خواص'
    },
    states: {
      new: 'جدید',
      current: 'جاری',
      deprecated: 'منسوخ شده است'
    },
    empty: {
      entities: 'هیچ موجودی برای این دسته یافت نشد.',
      details: 'یک موجودیت را برای بازرسی خواص انتخاب کنید.'
    }
  } as const;
