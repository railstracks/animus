export const interfaces = {
    title: 'इंटरफ़ेस प्रबंधन',
    actions: {
      refresh: 'रीफ़्रेश करें',
      add: 'इंटरफ़ेस जोड़ें',
      edit: 'संपादित करें',
      enable: 'सक्षम करें',
      disable: 'अक्षम करें',
      delete: 'हटाएँ',
      confirmDelete: 'इंटरफ़ेस "{name}" हटाएँ?'
    },
    columns: {
      name: 'नाम',
      type: 'प्रकार',
      module: 'मॉड्यूल ID',
      enabled: 'सक्षम',
      connected: 'कनेक्टेड',
      lastEvent: 'अंतिम इवेंट',
      actions: 'क्रियाएँ'
    },
    state: {
      enabled: 'हाँ',
      disabled: 'नहीं',
      connected: 'कनेक्टेड',
      disconnected: 'डिस्कनेक्टेड'
    },
    empty: 'अभी कोई इंटरफ़ेस कॉन्फ़िगर नहीं किया गया है।',
    form: {
      createTitle: 'इंटरफ़ेस बनाएँ',
      editTitle: 'इंटरफ़ेस संपादित करें: {name}',
      name: 'इंस्टेंस नाम',
      type: 'इंटरफ़ेस प्रकार',
      moduleId: 'मॉड्यूल ID',
      enabled: 'सक्षम',
      configJson: 'कॉन्फ़िग JSON',
      create: 'बनाएँ',
      save: 'सहेजें',
      cancel: 'रद्द करें',
      irc: {
        host: 'होस्ट',
        port: 'पोर्ट',
        nick: 'निक',
        serverPassword: 'सर्वर पासवर्ड',
        username: 'उपयोगकर्ता नाम',
        realname: 'वास्तविक नाम',
        dmOnly: 'केवल-DM मोड',
        respondToChannel: 'चैनल गतिविधि का उत्तर दें',
        respondToDm: 'प्रत्यक्ष संदेशों का उत्तर दें',
        respondToNotices: 'नोटिस का उत्तर दें',
        allowedDmUsers: 'अनुमत DM उपयोगकर्ता',
        allowedDmUsersHint: 'वैकल्पिक अनुमति सूची; कॉमा या नई पंक्ति से अलग।',
        agentId: 'एजेंट ID',
        reconnectEnabled: 'रीकनेक्ट सक्षम',
        reconnectInitialDelay: 'रीकनेक्ट प्रारंभिक विलंब (ms)',
        reconnectMaxDelay: 'रीकनेक्ट अधिकतम विलंब (ms)'
      }
    },
    createSuccess: 'इंटरफ़ेस "{name}" बनाया गया।',
    updateSuccess: 'इंटरफ़ेस "{name}" अपडेट किया गया।',
    deleteSuccess: 'इंटरफ़ेस "{name}" हटाया गया।'
  } as const;
