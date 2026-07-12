export const providers = {
    title: 'ការកំណត់រចនាសម្ព័ន្ធអ្នកផ្តល់សេវា',
    actions: {
      refresh: 'ធ្វើឱ្យស្រស់',
      add: 'បន្ថែមអ្នកផ្តល់សេវា',
      test: 'សាកល្បង',
      edit: 'កែសម្រួល',
      delete: 'លុប',
      setDefault: 'កំណត់លំនាំដើម',
      confirmDelete: 'លុបអ្នកផ្តល់សេវា "{id}"? នេះមិនអាចត្រឡប់វិញបានទេ។'
    },
    columns: {
      status: 'ស្ថានភាព',
      provider: 'ឈ្មោះ',
      type: 'ប្រភេទ',
      baseUrl: 'URL មូលដ្ឋាន',
      defaultModel: 'គំរូ',
      default: 'លំនាំដើម',
      actions: 'សកម្មភាព'
    },
    empty: {
      title: 'គ្មានអ្នកផ្តល់សេវា',
      description: 'បន្ថែមអ្នកផ្តល់សេវា LLM ដើម្បីចាប់ផ្តើម។'
    },
    form: {
      createTitle: 'បន្ថែមអ្នកផ្តល់សេវា',
      editTitle: 'កែសម្រួលអ្នកផ្តល់សេវា',
      providerType: 'ប្រភេទអ្នកផ្តល់សេវា',
      instanceName: 'ឈ្មោះឧទាហរណ៍',
      instanceNameHint: 'ឈ្មោះតែមួយគត់សម្រាប់ការកំណត់រចនាសម្ព័ន្ធអ្នកផ្តល់សេវានេះ។',
      baseUrl: 'URL មូលដ្ឋាន',
      defaultModel: 'គំរូលំនាំដើម',
      defaultContextWindow: 'បង្អួចបរិបទលំនាំដើម',
      modelContextWindows: 'ការបដិសេធបរិបទក្នុងមួយម៉ូដែល',
      modelContextWindowsHint: 'វត្ថុ JSON ឧ. gpt-5.2: 200000',
      modelContextWindowsInvalid: 'ការបដិសេធបរិបទក្នុងមួយម៉ូដែលត្រូវតែជា JSON ត្រឹមត្រូវ។',
      authType: 'ប្រភេទការផ្ទៀងផ្ទាត់',
      apiKey: 'សោ API',
      apiKeyPlaceholder: 'ទុក​ទទេ​ដើម្បី​រក្សា​សោ​បច្ចុប្បន្ន',
      authFile: 'ឯកសារផ្ទៀងផ្ទាត់',
      oauthBrowserTitle: 'ការចូលកម្មវិធីរុករកតាមអ៊ីនធឺណិត (លេខកូដអនុញ្ញាត)',
      oauthRedirectUri: 'ប្តូរទិស URI',
      oauthStartBrowser: 'ចាប់ផ្តើមការចូលកម្មវិធីរុករក',
      oauthAuthorizationUrl: 'URL អនុញ្ញាត',
      oauthState: 'រដ្ឋ',
      oauthCallbackUrl: 'បិទភ្ជាប់ URL ហៅត្រឡប់មកវិញ',
      oauthCallbackHint: 'បិទភ្ជាប់ URL ហៅត្រឡប់មកវិញពេញលេញដែលមានលេខកូដ និងស្ថានភាព។',
      oauthCompleteBrowser: 'ការចូលកម្មវិធីរុករកពេញលេញ',
      concurrency: 'ភាពស្របគ្នាអតិបរមា',
      create: 'បង្កើត',
      save: 'រក្សាទុក',
      cancel: 'បោះបង់',
      modelManualHint: 'បញ្ចូលលេខសម្គាល់ម៉ូដែលដោយដៃ។ ការចុះបញ្ជីម៉ូដែលមិនមានសម្រាប់ប្រភេទអ្នកផ្តល់សេវានេះទេ។',
      modelsLockedHint: 'រក្សាទុកអ្នកផ្តល់សេវាដោយប្រើសោ API ដើម្បីដោះសោការជ្រើសរើសម៉ូដែល។',
      savedContinue: 'អ្នកផ្តល់សេវាត្រូវបានរក្សាទុក។ ឥឡូវនេះអ្នកអាចជ្រើសរើសម៉ូដែលមួយ។',
      saveAndContinue: 'រក្សាទុក & បន្ត'
    },
    testSuccess: 'អ្នកផ្តល់សេវា "{id}" អាចទាក់ទងបាន។',
    testFailed: 'ការធ្វើតេស្ត "{id}" អ្នកផ្តល់សេវាបានបរាជ័យ',
    createSuccess: 'អ្នកផ្តល់សេវា "{id}" ត្រូវបានបង្កើតឡើង។',
    updateSuccess: 'អ្នកផ្តល់សេវា "{id}" បានធ្វើបច្ចុប្បន្នភាព។',
    deleteSuccess: 'អ្នកផ្តល់សេវា "{id}" ត្រូវបានលុប។',
    defaultSuccess: 'អ្នកផ្តល់សេវាលំនាំដើមបានកំណត់ទៅ "{id}" ។',
    errors: {
      loadFailed: 'បរាជ័យក្នុងការផ្ទុកអ្នកផ្តល់សេវា'
    }
  } as const;
