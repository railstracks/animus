export const interfaces = {
    title: 'مدیریت رابط‌ها',
    actions: {
      refresh: 'بازخوانی',
      add: 'افزودن رابط',
      edit: 'ویرایش',
      enable: 'فعال‌سازی',
      disable: 'غیرفعال‌سازی',
      delete: 'حذف',
      confirmDelete: 'رابط "{name}" حذف شود؟'
    },
    columns: {
      name: 'نام',
      type: 'نوع',
      module: 'شناسه ماژول',
      enabled: 'فعال',
      connected: 'متصل',
      lastEvent: 'آخرین رویداد',
      actions: 'اقدامات'
    },
    state: {
      enabled: 'بله',
      disabled: 'خیر',
      connected: 'متصل',
      disconnected: 'قطع‌شده'
    },
    empty: 'هنوز هیچ رابطی پیکربندی نشده است.',
    form: {
      createTitle: 'ایجاد رابط',
      editTitle: 'ویرایش رابط: {name}',
      name: 'نام نمونه',
      type: 'نوع رابط',
      moduleId: 'شناسه ماژول',
      enabled: 'فعال',
      configJson: 'JSON پیکربندی',
      create: 'ایجاد',
      save: 'ذخیره',
      cancel: 'لغو',
      irc: {
        host: 'میزبان',
        port: 'درگاه',
        nick: 'نام مستعار',
        serverPassword: 'رمز عبور سرور',
        username: 'نام کاربری',
        realname: 'نام واقعی',
        dmOnly: 'حالت فقط پیام مستقیم',
        respondToChannel: 'پاسخ به فعالیت کانال',
        respondToDm: 'پاسخ به پیام‌های مستقیم',
        respondToNotices: 'پاسخ به اعلان‌ها',
        allowedDmUsers: 'کاربران مجاز پیام مستقیم',
        allowedDmUsersHint: 'فهرست مجاز اختیاری؛ جداشده با ویرگول یا خط جدید.',
        agentId: 'شناسه عامل',
        reconnectEnabled: 'اتصال مجدد فعال',
        reconnectInitialDelay: 'تأخیر اولیه اتصال مجدد (میلی‌ثانیه)',
        reconnectMaxDelay: 'حداکثر تأخیر اتصال مجدد (میلی‌ثانیه)'
      }
    },
    createSuccess: 'رابط "{name}" ایجاد شد.',
    updateSuccess: 'رابط "{name}" به‌روزرسانی شد.',
    deleteSuccess: 'رابط "{name}" حذف شد.'
  } as const;
