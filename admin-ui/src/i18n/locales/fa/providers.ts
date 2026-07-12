export const providers = {
    title: 'پیکربندی ارائه دهنده',
    actions: {
      refresh: 'تازه کردن',
      add: 'ارائه دهنده را اضافه کنید',
      test: 'تست کنید',
      edit: 'ویرایش کنید',
      delete: 'حذف کنید',
      setDefault: 'تنظیم پیش فرض',
      confirmDelete: 'ارائه دهنده "{id}" حذف شود؟ این قابل واگرد نیست.'
    },
    columns: {
      status: 'وضعیت',
      provider: 'نام',
      type: 'تایپ کنید',
      baseUrl: 'URL پایه',
      defaultModel: 'مدل',
      default: 'پیش فرض',
      actions: 'اقدامات'
    },
    empty: {
      title: 'بدون ارائه دهندگان',
      description: 'برای شروع یک ارائه دهنده LLM اضافه کنید.'
    },
    form: {
      createTitle: 'ارائه دهنده را اضافه کنید',
      editTitle: 'ویرایش ارائه دهنده',
      providerType: 'نوع ارائه دهنده',
      instanceName: 'نام نمونه',
      instanceNameHint: 'یک نام منحصر به فرد برای این پیکربندی ارائه دهنده.',
      baseUrl: 'URL پایه',
      defaultModel: 'مدل پیش فرض',
      defaultContextWindow: 'پنجره زمینه پیش فرض',
      modelContextWindows: 'نادیده گرفتن زمینه هر مدل',
      modelContextWindowsHint: 'شی JSON، به عنوان مثال. gpt-5.2: 200000',
      modelContextWindowsInvalid: 'نادیده گرفتن زمینه هر مدل باید JSON معتبر باشد.',
      authType: 'نوع احراز هویت',
      apiKey: 'کلید API',
      apiKeyPlaceholder: 'برای حفظ کلید فعلی، خالی بگذارید',
      authFile: 'فایل Auth',
      oauthBrowserTitle: 'ورود به مرورگر (کد مجوز)',
      oauthRedirectUri: 'تغییر مسیر URI',
      oauthStartBrowser: 'ورود به مرورگر را شروع کنید',
      oauthAuthorizationUrl: 'URL مجوز',
      oauthState: 'ایالت',
      oauthCallbackUrl: 'URL بازگشت به تماس را جایگذاری کنید',
      oauthCallbackHint: 'URL کامل پاسخ به تماس حاوی کد و وضعیت را جای‌گذاری کنید.',
      oauthCompleteBrowser: 'ورود کامل به مرورگر',
      concurrency: 'حداکثر همزمانی',
      create: 'ایجاد کنید',
      save: 'ذخیره کنید',
      cancel: 'لغو کنید',
      modelManualHint: 'شناسه مدل را به صورت دستی وارد کنید. لیست مدل برای این نوع ارائه دهنده در دسترس نیست.',
      modelsLockedHint: 'برای باز کردن قفل انتخاب مدل، ارائه دهنده را با یک کلید API ذخیره کنید.',
      savedContinue: 'ارائه دهنده ذخیره شد. اکنون می توانید یک مدل را انتخاب کنید.',
      saveAndContinue: 'ذخیره و ادامه'
    },
    testSuccess: 'ارائه دهنده "{id}" قابل دسترسی است.',
    testFailed: 'تست ارائه دهنده "{id}" ناموفق بود',
    createSuccess: 'ارائه دهنده "{id}" ایجاد شد.',
    updateSuccess: 'ارائه دهنده "{id}" به روز شد.',
    deleteSuccess: 'ارائه دهنده "{id}" حذف شد.',
    defaultSuccess: 'ارائه دهنده پیش فرض روی "{id}" تنظیم شده است.',
    errors: {
      loadFailed: 'ارائه دهندگان بارگیری نشدند'
    }
  } as const;
