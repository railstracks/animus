export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'एनिमस में आपका स्वागत है।',
    welcomeSub: 'आरंभ करने के लिए अपना पहला एजेंट स्थापित करें।',
    beginSetup: 'सेटअप प्रारंभ करें →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'अपटाइम',
    agents: 'एजेंट',
    sessions: 'सत्र',
    providers: 'प्रदाता',
    errors: 'ग़लती',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'हाल के सत्र',
    viewAll: 'सभी देखें',
    empty: 'अभी तक कोई सत्र नहीं.',
    startChat: 'चैट प्रारंभ करें →',
    messages: 'संदेश',
  },
  // Memory card
  memory: {
    title: 'स्मृति',
    inspect: 'निरीक्षण करें',
    empty: 'कोई मेमोरी परत कॉन्फ़िगर नहीं की गई.',
    totalObservations: 'कुल अवलोकन',
  },
  // Provider card
  providers: {
    title: 'प्रदाता',
    manage: 'प्रबंधित करें',
    empty: 'कोई प्रदाता कॉन्फ़िगर नहीं किया गया.',
    runWizard: 'सेटअप विज़ार्ड चलाएँ →',
  },
  // Quick Actions card
  quickActions: {
    title: 'त्वरित कार्रवाई',
    newChat: 'नई चैट',
    addAgent: 'एजेंट जोड़ें',
    provider: 'प्रदाता',
    logs: 'लॉग',
    scheduler: 'अनुसूचक',
    memory: 'स्मृति',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'डेमॉन जानकारी',
    status: 'स्थिति',
    uptime: 'अपटाइम',
    agents: 'एजेंट',
    activeSessions: 'सक्रिय सत्र',
    providers: 'प्रदाता',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'स्मृति परतें',
    empty: 'कोई मेमोरी परत कॉन्फ़िगर नहीं की गई.',
  },
  // State labels
  state: {
    loading: 'लोड हो रहा है',
    unknown: 'अज्ञात',
  },
  // Error messages
  errors: {
    sessions: 'सत्र लोड करने में विफल',
    providers: 'प्रदाताओं को लोड करने में विफल',
    memory: 'मेमोरी एपीआई अनुपलब्ध',
  },
} as const;