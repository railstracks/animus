export const channels = {
    title: 'کانال ها',
    empty: 'هیچ کانالی پیکربندی نشده است. برای شروع یک مورد اضافه کنید.',
    createSuccess: 'کانال "{name}" با موفقیت ایجاد شد.',
    updateSuccess: 'کانال "{name}" با موفقیت به روز شد.',
    deleteSuccess: 'کانال "{name}" حذف شد.',
    columns: {
      name: 'نام',
      type: 'تایپ کنید',
      identity: 'هویت',
      endpoint: 'نقطه پایانی',
      enabled: 'فعال شد',
      connected: 'متصل شد',
      lastEvent: 'آخرین رویداد',
      actions: 'اقدامات'
    },
    state: {
      connected: 'متصل شد',
      disconnected: 'قطع شده است'
    },
    actions: {
      refresh: 'تازه کردن',
      add: 'افزودن کانال',
      cancel: 'لغو کنید',
      create: 'ایجاد کنید',
      save: 'ذخیره کنید',
      confirmDelete: 'کانال "{name}" حذف شود؟ این قابل واگرد نیست.'
    },
    form: {
      createTitle: 'افزودن کانال',
      editTitle: 'ویرایش "{name}"',
      name: 'نام کانال',
      type: 'نوع کانال',
      agent: 'عامل',
      minResponseInterval: 'حداقل فاصله پاسخ (ثانیه)',
      allowInterjection: 'اجازه مداخله',
      irc: {
        host: 'سرور IRC',
        port: 'بندر',
        serverPassword: 'رمز عبور سرور',
        nick: 'نام مستعار',
        username: 'نام کاربری',
        realname: 'نام واقعی',
        channels: 'کانال ها',
        channelsHint: 'هر خط یکی قالب: #channel [کلید]',
        agent: 'عامل',
        respondToChannel: 'به پیام های کانال پاسخ دهید',
        respondToDm: 'به پیام های مستقیم پاسخ دهید',
        respondToNotices: 'به اطلاعیه ها پاسخ دهید',
        allowedDmUsers: 'کاربران مجاز DM',
        reconnect: 'اتصال مجدد خودکار'
      },
      telegram: {
        botToken: 'توکن ربات',
        tokenHint: 'برای حفظ توکن موجود، آن را خالی بگذارید'
      },
      vk: {
        accessToken: 'نشانه دسترسی به انجمن',
        groupId: 'شناسه گروه'
      },
      bluesky: {
        handle: 'دسته',
        appPassword: 'رمز برنامه',
        pds: 'آدرس پی دی اس'
      },
      mastodon: {
        handle: 'دسته',
        instance: 'URL نمونه'
      },
      email: {
        apiKey: 'AgentMail API Key',
        apiKeyHint: 'برای حفظ کلید فعلی، خالی بگذارید',
        inboxId: 'شناسه صندوق ورودی',
        pollingWait: 'فاصله نظرسنجی (ثانیه)'
      },
      twitter: {
        tier: 'ردیف API',
        clientId: 'شناسه مشتری OAuth',
        clientSecret: 'راز مشتری OAuth',
        accessToken: 'رمز دسترسی',
        tokenHint: 'برای حفظ توکن موجود، آن را خالی بگذارید',
        refreshToken: 'Refresh Token',
        authorize: 'با توییتر مجوز دهید',
        oauthStarted: 'پنجره مجوز باز شد. جریان را در مرورگر خود کامل کنید.'
      },
      discord: {
        botToken: 'توکن ربات',
        tokenHint: 'برای حفظ توکن موجود، آن را خالی بگذارید',
        applicationId: 'شناسه برنامه (برای دستورات اسلش)',
        monitoredChannels: 'کانال‌های پیگیری شده',
        monitoredChannelsHint: 'شناسه کانال برای پیگیری زمینه (هر خط یک مورد). پیام‌ها بدون فعال کردن پاسخ ثبت می‌شوند.',
        respondToDm: 'به DM ها پاسخ دهید',
        respondToMentions: 'به ذکرها پاسخ دهید',
        monitorAllChannels: 'پیگیری همه کانال‌ها',
        respondToChannels: 'پاسخ به هر پیام در کانال‌های پیگیری شده',
        dmWhitelistEnabled: 'محدود کردن پیام مستقیم به کاربران مجاز',
        allowedDmUsers: 'کاربران مجاز پیام مستقیم',
        allowedDmUsersHint: 'نام‌های کاربری Discord (هر خط یک مورد). تنها این کاربران می‌توانند به ربات پیام مستقیم بفرستند.'
      },
      slack: {
        botToken: 'توکن ربات (xoxb-)',
        tokenHint: 'برای حفظ توکن موجود، آن را خالی بگذارید',
        appToken: 'توکن برنامه (xapp-)',
        appTokenHint: 'برای حالت سوکت (فاز 2). اختیاری برای فاز 1.',
        monitoredChannels: 'کانال های نظارت شده (نسخه)',
        monitoredChannelsHint: 'ربات تمام کانال هایی را که در آنها عضویت دارد به صورت خودکار نظارت می کند. فقط برای محدود کردن دامنه، شناسه‌ها را در اینجا اضافه کنید.',
        respondToMentions: "به {'@'}ذکرها پاسخ دهید",
        respondToAll: 'پاسخ به همه پیام‌ها (فیلتر ذکر را نادیده می‌گیرد)',
        threadedReplies: 'پاسخ های موضوع در کانال ها (هر پیام یک موضوع را شروع می کند)'
      }
    }
  } as const;
