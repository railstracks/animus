export const interfaces = {
    title: 'ইন্টারফেস ব্যবস্থাপনা',
    actions: {
      refresh: 'রিফ্রেশ',
      add: 'ইন্টারফেস যোগ করুন',
      edit: 'সম্পাদনা',
      enable: 'সক্রিয় করুন',
      disable: 'নিষ্ক্রিয় করুন',
      delete: 'মুছুন',
      confirmDelete: 'ইন্টারফেস "{name}" মুছবেন?'
    },
    columns: {
      name: 'নাম',
      type: 'ধরন',
      module: 'মডিউল ID',
      enabled: 'সক্রিয়',
      connected: 'সংযুক্ত',
      lastEvent: 'শেষ ইভেন্ট',
      actions: 'ক্রিয়া'
    },
    state: {
      enabled: 'হ্যাঁ',
      disabled: 'না',
      connected: 'সংযুক্ত',
      disconnected: 'সংযোগ বিচ্ছিন্ন'
    },
    empty: 'এখনও কোনো ইন্টারফেস কনফিগার করা হয়নি।',
    form: {
      createTitle: 'ইন্টারফেস তৈরি করুন',
      editTitle: 'ইন্টারফেস সম্পাদনা: {name}',
      name: 'ইনস্ট্যান্সের নাম',
      type: 'ইন্টারফেসের ধরন',
      moduleId: 'মডিউল ID',
      enabled: 'সক্রিয়',
      configJson: 'কনফিগ JSON',
      create: 'তৈরি করুন',
      save: 'সংরক্ষণ',
      cancel: 'বাতিল',
      irc: {
        host: 'হোস্ট',
        port: 'পোর্ট',
        nick: 'নিক',
        serverPassword: 'সার্ভার পাসওয়ার্ড',
        username: 'ব্যবহারকারীর নাম',
        realname: 'বাস্তব নাম',
        dmOnly: 'শুধু-DM মোড',
        respondToChannel: 'চ্যানেল কার্যকলাপে সাড়া দিন',
        respondToDm: 'সরাসরি বার্তায় সাড়া দিন',
        respondToNotices: 'নোটিসে সাড়া দিন',
        allowedDmUsers: 'অনুমোদিত DM ব্যবহারকারী',
        allowedDmUsersHint: 'ঐচ্ছিক allowlist; কমা বা নতুন লাইনে পৃথক করা।',
        agentId: 'এজেন্ট ID',
        reconnectEnabled: 'পুনঃসংযোগ সক্রিয়',
        reconnectInitialDelay: 'পুনঃসংযোগের প্রাথমিক বিলম্ব (ms)',
        reconnectMaxDelay: 'পুনঃসংযোগের সর্বোচ্চ বিলম্ব (ms)'
      }
    },
    createSuccess: 'ইন্টারফেস "{name}" তৈরি হয়েছে।',
    updateSuccess: 'ইন্টারফেস "{name}" আপডেট হয়েছে।',
    deleteSuccess: 'ইন্টারফেস "{name}" মুছে ফেলা হয়েছে।'
  } as const;
