export const providers = {
    title: 'प्रदाता विन्यास',
    actions: {
      refresh: 'ताज़ा करें',
      add: 'प्रदाता जोड़ें',
      test: 'परीक्षण',
      edit: 'संपादित करें',
      delete: 'हटाएँ',
      setDefault: 'डिफ़ॉल्ट सेट करें',
      confirmDelete: 'प्रदाता "{id}" को हटाएँ? इसे असंपादित नहीं किया जा सकता है।'
    },
    columns: {
      status: 'स्थिति',
      provider: 'नाम',
      type: 'प्रकार',
      baseUrl: 'आधार यूआरएल',
      defaultModel: 'मॉडल',
      default: 'डिफ़ॉल्ट',
      actions: 'क्रियाएँ'
    },
    empty: {
      title: 'कोई प्रदाता नहीं',
      description: 'आरंभ करने के लिए एक एलएलएम प्रदाता जोड़ें।'
    },
    form: {
      createTitle: 'प्रदाता जोड़ें',
      editTitle: 'प्रदाता संपादित करें',
      providerType: 'प्रदाता प्रकार',
      instanceName: 'उदाहरण का नाम',
      instanceNameHint: 'इस प्रदाता कॉन्फ़िगरेशन के लिए एक अद्वितीय नाम.',
      baseUrl: 'आधार यूआरएल',
      defaultModel: 'डिफ़ॉल्ट मॉडल',
      defaultContextWindow: 'डिफ़ॉल्ट संदर्भ विंडो',
      modelContextWindows: 'प्रति-मॉडल प्रसंग ओवरराइड',
      modelContextWindowsHint: 'JSON ऑब्जेक्ट, उदा. जीपीटी-5.2: 200000',
      modelContextWindowsInvalid: 'प्रति-मॉडल संदर्भ ओवरराइड वैध JSON होना चाहिए।',
      authType: 'प्रामाणिक प्रकार',
      apiKey: 'एपीआई कुंजी',
      apiKeyPlaceholder: 'वर्तमान कुंजी रखने के लिए खाली छोड़ें',
      authFile: 'प्रामाणिक फ़ाइल',
      oauthBrowserTitle: 'ब्राउज़र लॉगिन (प्राधिकरण कोड)',
      oauthRedirectUri: 'यूआरआई को पुनर्निर्देशित करें',
      oauthStartBrowser: 'ब्राउज़र लॉगिन प्रारंभ करें',
      oauthAuthorizationUrl: 'प्राधिकरण यूआरएल',
      oauthState: 'राज्य',
      oauthCallbackUrl: 'कॉलबैक यूआरएल चिपकाएँ',
      oauthCallbackHint: 'कोड और स्थिति वाला पूरा कॉलबैक यूआरएल चिपकाएँ।',
      oauthCompleteBrowser: 'पूर्ण ब्राउज़र लॉगिन',
      concurrency: 'अधिकतम समवर्ती',
      create: 'बनाएँ',
      save: 'सहेजें',
      cancel: 'रद्द करें',
      modelManualHint: 'मॉडल आईडी मैन्युअल रूप से दर्ज करें. इस प्रदाता प्रकार के लिए मॉडल सूची उपलब्ध नहीं है।',
      modelsLockedHint: 'मॉडल चयन को अनलॉक करने के लिए प्रदाता को एपीआई कुंजी के साथ सहेजें।',
      savedContinue: 'प्रदाता सहेजा गया. अब आप एक मॉडल का चयन कर सकते हैं.',
      saveAndContinue: 'सहेजें और जारी रखें'
    },
    testSuccess: 'प्रदाता "{id}" पहुंच योग्य है।',
    testFailed: 'प्रदाता "{id}" परीक्षण विफल रहा',
    createSuccess: 'प्रदाता "{id}" बनाया गया।',
    updateSuccess: 'प्रदाता "{id}" अद्यतन किया गया।',
    deleteSuccess: 'प्रदाता "{id}" हटा दिया गया।',
    defaultSuccess: 'डिफ़ॉल्ट प्रदाता "{id}" पर सेट है।',
    errors: {
      loadFailed: 'प्रदाताओं को लोड करने में विफल'
    }
  } as const;
