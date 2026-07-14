export const channels = {
    title: 'ឆានែល',
    empty: 'មិនមានបណ្តាញដែលបានកំណត់រចនាសម្ព័ន្ធទេ។ បន្ថែមមួយដើម្បីចាប់ផ្តើម។',
    createSuccess: 'ប៉ុស្តិ៍ "{name}" បានបង្កើតដោយជោគជ័យ។',
    updateSuccess: 'ប៉ុស្តិ៍ "{name}" បានធ្វើបច្ចុប្បន្នភាពដោយជោគជ័យ។',
    deleteSuccess: 'ឆានែល "{name}" ត្រូវបានលុប។',
    columns: {
      name: 'ឈ្មោះ',
      type: 'ប្រភេទ',
      identity: 'អត្តសញ្ញាណ',
      endpoint: 'ចំណុចបញ្ចប់',
      enabled: 'បានបើក',
      connected: 'បានភ្ជាប់',
      lastEvent: 'ព្រឹត្តិការណ៍ចុងក្រោយ',
      actions: 'សកម្មភាព'
    },
    state: {
      connected: 'បានភ្ជាប់',
      disconnected: 'បានផ្ដាច់'
    },
    actions: {
      refresh: 'ធ្វើឱ្យស្រស់',
      add: 'បន្ថែមឆានែល',
      cancel: 'បោះបង់',
      create: 'បង្កើត',
      save: 'រក្សាទុក',
      confirmDelete: 'លុបឆានែល "{name}"? នេះមិនអាចត្រឡប់វិញបានទេ។'
    },
    form: {
      createTitle: 'បន្ថែមឆានែល',
      editTitle: 'កែសម្រួល "{name}"',
      name: 'ឈ្មោះឆានែល',
      type: 'ប្រភេទឆានែល',
      agent: 'ភ្នាក់ងារ',
      minResponseInterval: 'ចន្លោះពេលឆ្លើយតបអប្បបរមា (វិនាទី)',
      allowInterjection: 'អនុញ្ញាតឱ្យចូលទៅជួយ',
      irc: {
        host: 'ម៉ាស៊ីនមេ IRC',
        port: 'ច្រក',
        serverPassword: 'ពាក្យសម្ងាត់ម៉ាស៊ីនមេ',
        nick: 'ឈ្មោះហៅក្រៅ',
        username: 'ឈ្មោះអ្នកប្រើប្រាស់',
        realname: 'ឈ្មោះពិត',
        channels: 'ឆានែល',
        channelsHint: 'មួយក្នុងមួយជួរ។ ទម្រង់៖ #channel [គន្លឹះ]',
        agent: 'ភ្នាក់ងារ',
        respondToChannel: 'ឆ្លើយតបទៅនឹងសាររបស់ប៉ុស្តិ៍',
        respondToDm: 'ឆ្លើយតបទៅនឹងសារផ្ទាល់',
        respondToNotices: 'ឆ្លើយតបទៅនឹងការជូនដំណឹង',
        allowedDmUsers: 'អ្នកប្រើប្រាស់ DM ត្រូវបានអនុញ្ញាត',
        reconnect: 'ភ្ជាប់ឡើងវិញដោយស្វ័យប្រវត្តិ'
      },
      telegram: {
        botToken: 'Bot Token',
        tokenHint: 'ទុក​ទទេ​ដើម្បី​រក្សា​សញ្ញា​សម្ងាត់​ដែល​មាន​ស្រាប់'
      },
      vk: {
        accessToken: 'និមិត្តសញ្ញាចូលប្រើសហគមន៍',
        groupId: 'លេខសម្គាល់ក្រុម'
      },
      bluesky: {
        handle: 'ដៃ',
        appPassword: 'ពាក្យសម្ងាត់កម្មវិធី',
        pds: 'PDS URL'
      },
      mastodon: {
        handle: 'ដៃ',
        instance: 'URL គំរូ'
      },
      email: {
        apiKey: 'AgentMail API Key',
        apiKeyHint: 'ទុក​ទទេ​ដើម្បី​រក្សា​សោ​បច្ចុប្បន្ន',
        inboxId: 'លេខសម្គាល់ប្រអប់សំបុត្រ',
        pollingWait: 'ចន្លោះពេលបោះឆ្នោត (វិនាទី)'
      },
      twitter: {
        tier: 'កម្រិត API',
        clientId: 'លេខសម្គាល់អតិថិជន OAuth',
        clientSecret: 'អាថ៌កំបាំងអតិថិជន OAuth',
        accessToken: 'ចូលប្រើនិមិត្តសញ្ញា',
        tokenHint: 'ទុក​ទទេ​ដើម្បី​រក្សា​សញ្ញា​សម្ងាត់​ដែល​មាន​ស្រាប់',
        refreshToken: 'ផ្ទុកថូខឹនឡើងវិញ',
        authorize: 'ផ្តល់សិទ្ធិជាមួយ Twitter',
        oauthStarted: 'បង្អួចអនុញ្ញាតបានបើក។ បំពេញលំហូរនៅក្នុងកម្មវិធីរុករករបស់អ្នក។'
      },
      discord: {
        botToken: 'Bot Token',
        tokenHint: 'ទុក​ទទេ​ដើម្បី​រក្សា​សញ្ញា​សម្ងាត់​ដែល​មាន​ស្រាប់',
        applicationId: 'លេខសម្គាល់កម្មវិធី (សម្រាប់ពាក្យបញ្ជាកាត់)',
        monitoredChannels: 'ប៉ុស្តិ៍ដែលបានត្រួតពិនិត្យ',
        monitoredChannelsHint: 'លេខសម្គាល់ឆានែលមួយក្នុងមួយជួរ។ Bot ត្រូវតែស្ថិតនៅក្នុងម៉ាស៊ីនមេ។',
        respondToDm: 'ឆ្លើយតបទៅ DMs',
        respondToMentions: 'ឆ្លើយតបទៅនឹងការលើកឡើង'
      },
      slack: {
        botToken: 'Bot Token (xoxb-)',
        tokenHint: 'ទុក​ទទេ​ដើម្បី​រក្សា​សញ្ញា​សម្ងាត់​ដែល​មាន​ស្រាប់',
        appToken: 'និមិត្តសញ្ញាកម្មវិធី (xapp-)',
        appTokenHint: 'សម្រាប់របៀបរន្ធ (ដំណាក់កាលទី 2) ។ ជាជម្រើសសម្រាប់ដំណាក់កាលទី 1 ។',
        monitoredChannels: 'ប៉ុស្តិ៍ដែលបានត្រួតពិនិត្យ (បដិសេធ)',
        monitoredChannelsHint: 'Bot ត្រួតពិនិត្យដោយស្វ័យប្រវត្តិគ្រប់ប៉ុស្តិ៍ទាំងអស់ដែលវាជាសមាជិក។ មានតែបន្ថែមលេខសម្គាល់នៅទីនេះដើម្បីដាក់កម្រិតវិសាលភាព។',
        respondToMentions: "ឆ្លើយតបទៅនឹង {'@'} ការលើកឡើង",
        respondToAll: 'ឆ្លើយតបទៅនឹងសារទាំងអស់ (មិនអើពើនឹងការលើកឡើងតម្រង)',
        threadedReplies: 'ការ​ឆ្លើយ​តប​ខ្សែ​ស្រឡាយ​នៅ​ក្នុង​ឆានែល (សារ​នីមួយៗ​ចាប់​ផ្ដើម​ខ្សែ​ស្រឡាយ)'
      }
    }
  } as const;
