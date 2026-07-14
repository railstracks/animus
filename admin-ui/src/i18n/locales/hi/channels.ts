export const channels = {
    title: 'चैनल',
    empty: 'कोई चैनल कॉन्फ़िगर नहीं किया गया. आरंभ करने के लिए एक जोड़ें.',
    createSuccess: 'चैनल "{name}" सफलतापूर्वक बनाया गया।',
    updateSuccess: 'चैनल "{name}" सफलतापूर्वक अपडेट किया गया।',
    deleteSuccess: 'चैनल "{name}" हटा दिया गया.',
    columns: {
      name: 'नाम',
      type: 'प्रकार',
      identity: 'पहचान',
      endpoint: 'समापन बिंदु',
      enabled: 'सक्षम',
      connected: 'जुड़ा हुआ',
      lastEvent: 'अंतिम घटना',
      actions: 'क्रियाएँ'
    },
    state: {
      connected: 'जुड़ा हुआ',
      disconnected: 'विच्छेदित'
    },
    actions: {
      refresh: 'ताज़ा करें',
      add: 'चैनल जोड़ें',
      cancel: 'रद्द करें',
      create: 'बनाएँ',
      save: 'सहेजें',
      confirmDelete: 'चैनल "{name}" हटाएं? इसे असंपादित नहीं किया जा सकता है।'
    },
    form: {
      createTitle: 'चैनल जोड़ें',
      editTitle: '"{name}" संपादित करें',
      name: 'चैनल का नाम',
      type: 'चैनल प्रकार',
      agent: 'एजेंट',
      minResponseInterval: 'न्यूनतम प्रतिक्रिया अंतराल (सेकंड)',
      allowInterjection: 'क्षेपण की अनुमति दें',
      irc: {
        host: 'आईआरसी सर्वर',
        port: 'बंदरगाह',
        serverPassword: 'सर्वर पासवर्ड',
        nick: 'उपनाम',
        username: 'उपयोगकर्ता नाम',
        realname: 'वास्तविक नाम',
        channels: 'चैनल',
        channelsHint: 'प्रति पंक्ति एक. प्रारूप: #चैनल [कुंजी]',
        agent: 'एजेंट',
        respondToChannel: 'चैनल संदेशों का जवाब दें',
        respondToDm: 'सीधे संदेशों का जवाब दें',
        respondToNotices: 'नोटिस का जवाब दें',
        allowedDmUsers: 'डीएम उपयोगकर्ताओं को अनुमति है',
        reconnect: 'स्वतः पुनः कनेक्ट करें'
      },
      telegram: {
        botToken: 'बॉट टोकन',
        tokenHint: 'मौजूदा टोकन रखने के लिए खाली छोड़ दें'
      },
      vk: {
        accessToken: 'सामुदायिक पहुंच टोकन',
        groupId: 'ग्रुप आईडी'
      },
      bluesky: {
        handle: 'संभाल',
        appPassword: 'ऐप पासवर्ड',
        pds: 'पीडीएस यूआरएल'
      },
      mastodon: {
        handle: 'संभाल',
        instance: 'उदाहरण यूआरएल'
      },
      email: {
        apiKey: 'एजेंटमेल एपीआई कुंजी',
        apiKeyHint: 'वर्तमान कुंजी रखने के लिए खाली छोड़ें',
        inboxId: 'इनबॉक्स आईडी',
        pollingWait: 'मतदान अंतराल (सेकंड)'
      },
      twitter: {
        tier: 'एपीआई टियर',
        clientId: 'OAuth क्लाइंट आईडी',
        clientSecret: 'OAuth क्लाइंट रहस्य',
        accessToken: 'प्रवेश टोकन',
        tokenHint: 'मौजूदा टोकन रखने के लिए खाली छोड़ दें',
        refreshToken: 'टोकन ताज़ा करें',
        authorize: 'ट्विटर से अधिकृत करें',
        oauthStarted: 'प्राधिकरण विंडो खोली गई. अपने ब्राउज़र में प्रवाह पूरा करें.'
      },
      discord: {
        botToken: 'बॉट टोकन',
        tokenHint: 'मौजूदा टोकन रखने के लिए खाली छोड़ दें',
        applicationId: 'एप्लिकेशन आईडी (स्लैश कमांड के लिए)',
        monitoredChannels: 'निगरानी किये गये चैनल',
        monitoredChannelsHint: 'प्रति पंक्ति एक चैनल आईडी. बॉट सर्वर में होना चाहिए.',
        respondToDm: 'डीएम को जवाब दें',
        respondToMentions: 'उल्लेखों पर प्रतिक्रिया दें'
      },
      slack: {
        botToken: 'बॉट टोकन (xoxb-)',
        tokenHint: 'मौजूदा टोकन रखने के लिए खाली छोड़ दें',
        appToken: 'ऐप टोकन (xapp-)',
        appTokenHint: 'सॉकेट मोड (चरण 2) के लिए। चरण 1 के लिए वैकल्पिक.',
        monitoredChannels: 'मॉनिटर किए गए चैनल (ओवरराइड)',
        monitoredChannelsHint: 'बॉट उन सभी चैनलों की स्वतः निगरानी करता है जिनका वह सदस्य है। दायरा सीमित करने के लिए यहां केवल आईडी जोड़ें।',
        respondToMentions: "{'@'}उल्लेखों का जवाब दें",
        respondToAll: 'सभी संदेशों का उत्तर दें (उल्लेख फ़िल्टर को अनदेखा करें)',
        threadedReplies: 'चैनलों में थ्रेड उत्तर देता है (प्रत्येक संदेश एक थ्रेड प्रारंभ करता है)'
      }
    }
  } as const;
