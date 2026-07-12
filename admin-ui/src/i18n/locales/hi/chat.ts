export const chat = {
    sessionLabel: 'सत्र',
    newSession: 'नया सत्र',
    refresh: 'ताज़ा करें',
    stopGenerating: 'उत्पन्न करना बंद करो',
    emptyTitle: 'बातचीत शुरू करें',
    emptyDescription: 'शुरू करने के लिए एनिमस से मेमोरी, कॉन्फ़िगरेशन या लाइव सत्र के बारे में पूछें।',
    streamState: {
      streaming: 'स्ट्रीमिंग...',
      stopped: 'रुक गया'
    },
    jumpToLatest: 'नवीनतम पर जाएं',
    composerPlaceholder: 'एनिमस को एक संदेश भेजें...',
    adminTokenLabel: 'व्यवस्थापक टोकन (वैकल्पिक)',
    send: 'भेजें',
    contextTitle: 'प्रसंग',
    context: {
      session: 'सत्र',
      layers: 'परतें',
      tools: 'उपकरण',
      budget: 'बजट',
      fallbackNote: 'सत्र बदलते ही संदर्भ स्नैपशॉट अपडेट हो जाता है।'
    },
    sidebarTabs: {
      context: 'प्रसंग',
      sessions: 'सत्र',
      messages: 'संदेश',
      noSessions: 'अभी तक कोई सत्र उपलब्ध नहीं है.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'नया थ्रेड प्रारंभ करने के लिए एक संदेश लिखें.'
    },
    thinking: {
      label: "सोच रहा हूँ"
    },
    toolResult: {
      label: "उपकरण परिणाम"
    },
    toolCall: {
      label: "टूल कॉल"
    },
    reasoning: {
      label: 'तर्क विधा',
      enabled: 'पर',
      disabled: 'बंद',
      effort: 'प्रयास',
      instructionLabel: 'सिस्टम अनुदेश',
      instructionPlaceholder: 'कस्टम तर्क निर्देश (खाली होने पर डिफ़ॉल्ट का उपयोग करता है)',
      notSupported: 'इस मॉडल के लिए तर्क उपलब्ध नहीं है.'
    },
    provider: {
      label: 'प्रदाता',
      select: 'प्रदाता',
      model: 'मॉडल'
    },
    agent: {
      label: 'एजेंट (नया सत्र)'
    },
    status: {
      closed: 'बंद',
      connecting: 'जुड़ रहा है',
      open: 'जुड़ा हुआ'
    },
    errors: {
      websocket: 'वेबसॉकेट त्रुटि',
      websocketNotConnected: 'WebSocket अभी तक कनेक्ट नहीं हुआ है. कृपया पुन: प्रयास करें।',
      agentNotReady: 'एजेंट चयन अभी भी लोड हो रहा है. कृपया एक क्षण प्रतीक्षा करें और पुनः प्रयास करें।',
      unknownWebsocket: 'अज्ञात वेबसोकेट त्रुटि',
      sessionLoadFailed: 'सत्र {sessionId} लोड करने में विफल: {message}'
    }
  } as const;
