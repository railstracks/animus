export const chat = {
    sessionLabel: 'অধিবেশন',
    newSession: 'নতুন অধিবেশন',
    refresh: 'রিফ্রেশ',
    stopGenerating: 'জেনারেট করা বন্ধ করুন',
    emptyTitle: 'একটি কথোপকথন শুরু করুন',
    emptyDescription: 'অ্যানিমাসকে মেমরি, কনফিগারেশন বা রোলিং পেতে একটি লাইভ সেশন সম্পর্কে জিজ্ঞাসা করুন।',
    streamState: {
      streaming: 'স্ট্রিমিং...',
      stopped: 'থামানো'
    },
    jumpToLatest: 'সর্বশেষে ঝাঁপ দাও',
    composerPlaceholder: 'অ্যানিমাসকে একটি বার্তা পাঠান...',
    adminTokenLabel: 'অ্যাডমিন টোকেন (ঐচ্ছিক)',
    send: 'পাঠান',
    contextTitle: 'প্রসঙ্গ',
    context: {
      session: 'অধিবেশন',
      layers: 'স্তর',
      tools: 'টুলস',
      budget: 'বাজেট',
      fallbackNote: 'সেশন পরিবর্তনের সাথে সাথে প্রসঙ্গ স্ন্যাপশট আপডেট হয়।'
    },
    sidebarTabs: {
      context: 'প্রসঙ্গ',
      sessions: 'সেশন',
      messages: 'বার্তা',
      noSessions: 'এখনও কোন সেশন উপলব্ধ.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'একটি নতুন থ্রেড শুরু করতে একটি বার্তা রচনা করুন.'
    },
    thinking: {
      label: "ভাবছেন"
    },
    toolResult: {
      label: "টুল ফলাফল"
    },
    toolCall: {
      label: "টুল কল"
    },
    reasoning: {
      label: 'যুক্তি মোড',
      enabled: 'চালু',
      disabled: 'বন্ধ',
      effort: 'প্রচেষ্টা',
      instructionLabel: 'সিস্টেমের নির্দেশনা',
      instructionPlaceholder: 'কাস্টম যুক্তি নির্দেশনা (খালি থাকলে ডিফল্ট ব্যবহার করে)',
      notSupported: 'এই মডেলের জন্য যুক্তি উপলব্ধ নয়।'
    },
    provider: {
      label: 'প্রদানকারী',
      select: 'প্রদানকারী',
      model: 'মডেল'
    },
    agent: {
      label: 'এজেন্ট (নতুন অধিবেশন)'
    },
    status: {
      closed: 'বন্ধ',
      connecting: 'সংযোগ করা হচ্ছে',
      open: 'সংযুক্ত'
    },
    errors: {
      websocket: 'ওয়েবসকেট ত্রুটি',
      websocketNotConnected: 'WebSocket এখনও সংযুক্ত করা হয়নি. আবার চেষ্টা করুন.',
      agentNotReady: 'এজেন্ট নির্বাচন এখনও লোড হচ্ছে. অনুগ্রহ করে কিছুক্ষণ অপেক্ষা করুন এবং আবার চেষ্টা করুন।',
      unknownWebsocket: 'অজানা ওয়েবসকেট ত্রুটি৷',
      sessionLoadFailed: 'অধিবেশন লোড করতে ব্যর্থ হয়েছে {sessionId}: {message}'
    }
  } as const;
