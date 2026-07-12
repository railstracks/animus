export const channels = {
    title: 'Tashoshi',
    empty: 'Babu tashoshi da aka saita. Ƙara ɗaya don farawa.',
    createSuccess: 'An kirkiri tashar "{name}" cikin nasara.',
    updateSuccess: 'An sabunta tashar "{name}" cikin nasara.',
    deleteSuccess: 'Tashar "{name}" an goge.',
    columns: {
      name: 'Suna',
      type: 'Nau\'in',
      identity: 'Shaida',
      endpoint: 'Ƙarshen Ƙarshe',
      enabled: 'An kunna',
      connected: 'An haɗa',
      lastEvent: 'Lamarin Karshe',
      actions: 'Ayyuka'
    },
    state: {
      connected: 'An haɗa',
      disconnected: 'An cire haɗin'
    },
    actions: {
      refresh: 'Sake sabuntawa',
      add: 'Ƙara Channel',
      cancel: 'Soke',
      create: 'Ƙirƙiri',
      save: 'Ajiye',
      confirmDelete: 'Share tashar "{name}"? Ba za a iya soke wannan ba.'
    },
    form: {
      createTitle: 'Ƙara Channel',
      editTitle: 'Gyara "{name}"',
      name: 'Sunan tashar',
      type: 'Nau\'in Tashoshi',
      agent: 'Wakili',
      irc: {
        host: 'Sabar IRC',
        port: 'Port',
        serverPassword: 'Kalmar wucewa ta uwar garken',
        nick: 'Laƙabi',
        username: 'Sunan mai amfani',
        realname: 'Sunan Gaskiya',
        channels: 'Tashoshi',
        channelsHint: 'Daya kowane layi. Tsarin: #channel [key]',
        agent: 'Wakili',
        respondToChannel: 'Amsa saƙonnin tashoshi',
        respondToDm: 'Amsa saƙonnin kai tsaye',
        respondToNotices: 'Amsa ga sanarwa',
        allowedDmUsers: 'An halatta masu amfani da DM',
        reconnect: 'Sake haɗawa ta atomatik'
      },
      telegram: {
        botToken: 'Bot Token',
        tokenHint: 'Bar komai don kiyaye alamar da ke akwai'
      },
      vk: {
        accessToken: 'Alamar Samun Al\'umma',
        groupId: 'ID na rukuni'
      },
      bluesky: {
        handle: 'Hannu',
        appPassword: 'Kalmar wucewa ta App',
        pds: 'PDS URL'
      },
      mastodon: {
        handle: 'Hannu',
        instance: 'Misali URL'
      },
      email: {
        apiKey: 'Maɓallin API AgentMail',
        apiKeyHint: 'Bar komai don kiyaye maɓallin yanzu',
        inboxId: 'ID akwatin saƙo',
        pollingWait: 'Tazarar Zaɓe (daƙiƙa)'
      },
      twitter: {
        tier: 'Tier API',
        clientId: 'ID Client OAuth',
        clientSecret: 'Asirin Abokin Ciniki na OAuth',
        accessToken: 'Alamar shiga',
        tokenHint: 'Bar komai don kiyaye alamar da ke akwai',
        refreshToken: 'Refresh Token',
        authorize: 'Yi izini tare da Twitter',
        oauthStarted: 'An buɗe taga izini. Kammala kwarara a cikin burauzarka.'
      },
      discord: {
        botToken: 'Bot Token',
        tokenHint: 'Bar komai don kiyaye alamar da ke akwai',
        applicationId: 'ID na aikace-aikacen (don umarnin slash)',
        monitoredChannels: 'Tashoshi masu kulawa',
        monitoredChannelsHint: 'ID tasha ɗaya akan kowane layi. Bot dole ne ya kasance a cikin uwar garken.',
        respondToDm: 'Amsa ga DMs',
        respondToMentions: 'Amsa ga ambaton'
      },
      slack: {
        botToken: 'Bot Token (xoxb-)',
        tokenHint: 'Bar komai don kiyaye alamar da ke akwai',
        appToken: 'App Token (xapp-)',
        appTokenHint: 'Don Yanayin Socket (Mataki na 2). Zabi na Mataki na 1.',
        monitoredChannels: 'Tashoshin Kulawa (an soke)',
        monitoredChannelsHint: 'Bot yana sa ido kan duk tashoshi memba na su. Ƙara ID kawai anan don taƙaita iyaka.',
        respondToMentions: "Amsa ga {'@'} ambaton",
        respondToAll: 'Amsa ga duk saƙonni (yi watsi da ambaton tace)',
        threadedReplies: 'Zare yana ba da amsa a cikin tashoshi (kowane saƙo yana fara zaren)'
      }
    }
  } as const;
