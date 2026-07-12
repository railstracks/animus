export const providers = {
    title: 'Kanfigareshan Mai bayarwa',
    actions: {
      refresh: 'Sake sabuntawa',
      add: 'Ƙara Mai bayarwa',
      test: 'Gwaji',
      edit: 'Gyara',
      delete: 'Share',
      setDefault: 'Saita Default',
      confirmDelete: 'Cire mai bada "{id}"? Ba za a iya soke wannan ba.'
    },
    columns: {
      status: 'Matsayi',
      provider: 'Suna',
      type: 'Nau\'in',
      baseUrl: 'Tushen URL',
      defaultModel: 'Samfura',
      default: 'Tsohuwar',
      actions: 'Ayyuka'
    },
    empty: {
      title: 'Babu Masu bayarwa',
      description: 'Ƙara mai bada LLM don farawa.'
    },
    form: {
      createTitle: 'Ƙara Mai bayarwa',
      editTitle: 'Gyara Mai bayarwa',
      providerType: 'Nau\'in Mai bayarwa',
      instanceName: 'Misali Suna',
      instanceNameHint: 'Suna na musamman don wannan daidaitawar mai bada sabis.',
      baseUrl: 'Tushen URL',
      defaultModel: 'Samfurin Default',
      defaultContextWindow: 'Tsohuwar Tagar Magana',
      modelContextWindows: 'Matsayin Samfuran Yana Hakuri',
      modelContextWindowsHint: 'JSON abu, misali. gpt-5.2: 200000',
      modelContextWindowsInvalid: 'Haɓaka mahallin kowane samfuri dole ne ya zama ingantacciyar JSON.',
      authType: 'Nau\'in Gaskiya',
      apiKey: 'Maɓallin API',
      apiKeyPlaceholder: 'Bar komai don kiyaye maɓallin yanzu',
      authFile: 'Fayil na Gaskiya',
      oauthBrowserTitle: 'Shigar Mai lilo (Lambar izini)',
      oauthRedirectUri: 'Juyawa URI',
      oauthStartBrowser: 'Fara Shiga Mai lilo',
      oauthAuthorizationUrl: 'URL na izini',
      oauthState: 'Jiha',
      oauthCallbackUrl: 'Manna URL ɗin Kira',
      oauthCallbackHint: 'Manna cikakken URL ɗin kiran da ke ɗauke da lamba da jiha.',
      oauthCompleteBrowser: 'Cikakkun Shiga Mai Binciken Bincike',
      concurrency: 'Max Concurrency',
      create: 'Ƙirƙiri',
      save: 'Ajiye',
      cancel: 'Soke',
      modelManualHint: 'Shigar da samfurin ID da hannu. Babu lissafin samfurin don wannan nau\'in mai bayarwa.',
      modelsLockedHint: 'Ajiye mai badawa tare da maɓallin API don buɗe zaɓin ƙira.',
      savedContinue: 'Ajiye mai bayarwa. Yanzu zaku iya zaɓar samfuri.',
      saveAndContinue: 'Ajiye & Ci gaba'
    },
    testSuccess: 'Ana iya samun mai bayarwa "{id}"',
    testFailed: 'Gwajin mai bayarwa "{id}" ta kasa',
    createSuccess: 'An ƙirƙira mai bada "{id}".',
    updateSuccess: 'An sabunta mai bada "{id}"',
    deleteSuccess: 'An share mai bada "{id}".',
    defaultSuccess: 'An saita tsoho mai bada sabis zuwa "{id}".',
    errors: {
      loadFailed: 'An kasa loda masu samarwa'
    }
  } as const;
