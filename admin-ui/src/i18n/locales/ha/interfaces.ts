export const interfaces = {
    title: 'Gudanar da Fuskoki',
    actions: {
      refresh: 'Sabunta',
      add: 'Ƙara Fuska',
      edit: 'Gyara',
      enable: 'Kunna',
      disable: 'Kashe',
      delete: 'Share',
      confirmDelete: 'Share fuska "{name}"?'
    },
    columns: {
      name: 'Suna',
      type: 'Nau’i',
      module: 'ID na Module',
      enabled: 'An kunna',
      connected: 'An haɗa',
      lastEvent: 'Abu na Ƙarshe',
      actions: 'Ayyuka'
    },
    state: {
      enabled: 'eh',
      disabled: 'a’a',
      connected: 'an haɗa',
      disconnected: 'an cire haɗi'
    },
    empty: 'Ba a saita wata fuska ba tukuna.',
    form: {
      createTitle: 'Ƙirƙiri Fuska',
      editTitle: 'Gyara Fuska: {name}',
      name: 'Sunan Instance',
      type: 'Nau’in Fuska',
      moduleId: 'ID na Module',
      enabled: 'An kunna',
      configJson: 'JSON na Saituna',
      create: 'Ƙirƙira',
      save: 'Ajiye',
      cancel: 'Soke',
      irc: {
        host: 'Host',
        port: 'Port',
        nick: 'Laƙabi',
        serverPassword: 'Kalmar Sirrin Sabar',
        username: 'Sunan mai amfani',
        realname: 'Suna na Gaskiya',
        dmOnly: 'Yanayin DM Kawai',
        respondToChannel: 'Amsa Ayyukan Tashoshi',
        respondToDm: 'Amsa Saƙonnin Kai Tsaye',
        respondToNotices: 'Amsa Sanarwa',
        allowedDmUsers: 'Masu Amfani da Aka Yarda da DM',
        allowedDmUsersHint: 'Jerin izini na zaɓi; raba da wakafi ko sabon layi.',
        agentId: 'ID na Wakili',
        reconnectEnabled: 'An Kunna Sake Haɗawa',
        reconnectInitialDelay: 'Jinkirin Farko na Sake Haɗawa (ms)',
        reconnectMaxDelay: 'Matsakaicin Jinkirin Sake Haɗawa (ms)'
      }
    },
    createSuccess: 'An ƙirƙiri fuska "{name}".',
    updateSuccess: 'An sabunta fuska "{name}".',
    deleteSuccess: 'An share fuska "{name}".'
  } as const;
