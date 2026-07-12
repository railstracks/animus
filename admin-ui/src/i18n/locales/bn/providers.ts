export const providers = {
    title: 'প্রদানকারী কনফিগারেশন',
    actions: {
      refresh: 'রিফ্রেশ',
      add: 'প্রদানকারী যোগ করুন',
      test: 'পরীক্ষা',
      edit: 'সম্পাদনা করুন',
      delete: 'মুছুন',
      setDefault: 'ডিফল্ট সেট করুন',
      confirmDelete: 'প্রদানকারী "{id}" সরাবেন? এটি পূর্বাবস্থায় ফেরানো যাবে না।'
    },
    columns: {
      status: 'স্ট্যাটাস',
      provider: 'নাম',
      type: 'টাইপ',
      baseUrl: 'বেস ইউআরএল',
      defaultModel: 'মডেল',
      default: 'ডিফল্ট',
      actions: 'কর্ম'
    },
    empty: {
      title: 'কোন প্রদানকারী',
      description: 'শুরু করতে একজন LLM প্রদানকারী যোগ করুন।'
    },
    form: {
      createTitle: 'প্রদানকারী যোগ করুন',
      editTitle: 'প্রদানকারী সম্পাদনা করুন',
      providerType: 'প্রদানকারীর ধরন',
      instanceName: 'উদাহরণের নাম',
      instanceNameHint: 'এই প্রদানকারী কনফিগারেশনের জন্য একটি অনন্য নাম।',
      baseUrl: 'বেস ইউআরএল',
      defaultModel: 'ডিফল্ট মডেল',
      defaultContextWindow: 'ডিফল্ট প্রসঙ্গ উইন্ডো',
      modelContextWindows: 'প্রতি-মডেল প্রসঙ্গ ওভাররাইড',
      modelContextWindowsHint: 'JSON অবজেক্ট, যেমন gpt-5.2: 200000',
      modelContextWindowsInvalid: 'প্রতি-মডেল প্রসঙ্গ ওভাররাইড অবশ্যই বৈধ JSON হতে হবে।',
      authType: 'প্রমাণের ধরন',
      apiKey: 'API কী',
      apiKeyPlaceholder: 'বর্তমান কী রাখতে খালি ছেড়ে দিন',
      authFile: 'প্রমাণীকরণ ফাইল',
      oauthBrowserTitle: 'ব্রাউজার লগইন (অনুমোদন কোড)',
      oauthRedirectUri: 'ইউআরআই পুনর্নির্দেশ করুন',
      oauthStartBrowser: 'ব্রাউজার লগইন শুরু করুন',
      oauthAuthorizationUrl: 'অনুমোদন URL',
      oauthState: 'রাজ্য',
      oauthCallbackUrl: 'কলব্যাক URL আটকান',
      oauthCallbackHint: 'কোড এবং রাজ্য সম্বলিত পূর্ণ কলব্যাক URL পেস্ট করুন।',
      oauthCompleteBrowser: 'সম্পূর্ণ ব্রাউজার লগইন',
      concurrency: 'সর্বোচ্চ সঙ্গতি',
      create: 'তৈরি করুন',
      save: 'সংরক্ষণ করুন',
      cancel: 'বাতিল করুন',
      modelManualHint: 'ম্যানুয়ালি মডেল আইডি লিখুন। এই প্রদানকারী ধরনের জন্য মডেল তালিকা উপলব্ধ নয়.',
      modelsLockedHint: 'মডেল নির্বাচন আনলক করতে একটি API কী সহ প্রদানকারীকে সংরক্ষণ করুন৷',
      savedContinue: 'প্রদানকারী সংরক্ষিত. আপনি এখন একটি মডেল নির্বাচন করতে পারেন.',
      saveAndContinue: 'সংরক্ষণ করুন এবং চালিয়ে যান'
    },
    testSuccess: 'প্রদানকারী "{id}" পৌঁছানো যায়।',
    testFailed: 'প্রদানকারী "{id}" পরীক্ষা ব্যর্থ হয়েছে৷',
    createSuccess: 'প্রদানকারী "{id}" তৈরি করা হয়েছে৷',
    updateSuccess: 'প্রদানকারী "{id}" আপডেট করা হয়েছে।',
    deleteSuccess: 'প্রদানকারী "{id}" মোছা হয়েছে৷',
    defaultSuccess: 'ডিফল্ট প্রদানকারী "{id}" এ সেট করা হয়েছে।',
    errors: {
      loadFailed: 'প্রদানকারী লোড করতে ব্যর্থ হয়েছে'
    }
  } as const;
