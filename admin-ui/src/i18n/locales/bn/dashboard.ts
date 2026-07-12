export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'অ্যানিমাসে স্বাগতম।',
    welcomeSub: 'শুরু করার জন্য আপনার প্রথম এজেন্ট সেট আপ করুন।',
    beginSetup: 'সেটআপ শুরু করুন →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'আপটাইম',
    agents: 'এজেন্ট',
    sessions: 'সেশন',
    providers: 'প্রদানকারী',
    errors: 'ভুল',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'সাম্প্রতিক সেশন',
    viewAll: 'সব দেখুন',
    empty: 'এখনো কোনো সেশন নেই।',
    startChat: 'একটি চ্যাট শুরু করুন →',
    messages: 'বার্তা',
  },
  // Memory card
  memory: {
    title: 'স্মৃতি',
    inspect: 'পরিদর্শন করুন',
    empty: 'কোনো মেমরি স্তর কনফিগার করা নেই.',
    totalObservations: 'মোট পর্যবেক্ষণ',
  },
  // Provider card
  providers: {
    title: 'প্রদানকারী',
    manage: 'পরিচালনা করুন',
    empty: 'কোন প্রদানকারী কনফিগার করা নেই.',
    runWizard: 'সেটআপ উইজার্ড চালান →',
  },
  // Quick Actions card
  quickActions: {
    title: 'দ্রুত অ্যাকশন',
    newChat: 'নতুন চ্যাট',
    addAgent: 'এজেন্ট যোগ করুন',
    provider: 'প্রদানকারী',
    logs: 'লগ',
    scheduler: 'সময়সূচী',
    memory: 'স্মৃতি',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'ডেমন তথ্য',
    status: 'স্ট্যাটাস',
    uptime: 'আপটাইম',
    agents: 'এজেন্ট',
    activeSessions: 'সক্রিয় সেশন',
    providers: 'প্রদানকারী',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'মেমরি স্তর',
    empty: 'কোনো মেমরি স্তর কনফিগার করা নেই.',
  },
  // State labels
  state: {
    loading: 'লোড হচ্ছে',
    unknown: 'অজানা',
  },
  // Error messages
  errors: {
    sessions: 'সেশনগুলি লোড করতে ব্যর্থ হয়েছে৷',
    providers: 'প্রদানকারী লোড করতে ব্যর্থ হয়েছে',
    memory: 'মেমরি API অনুপলব্ধ',
  },
} as const;