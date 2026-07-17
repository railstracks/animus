export const channels = {
    title: 'চ্যানেল',
    empty: 'কোন চ্যানেল কনফিগার করা নেই. শুরু করতে একটি যোগ করুন.',
    createSuccess: 'চ্যানেল "{name}" সফলভাবে তৈরি হয়েছে৷',
    updateSuccess: 'চ্যানেল "{name}" সফলভাবে আপডেট হয়েছে৷',
    deleteSuccess: 'চ্যানেল "{name}" মুছে ফেলা হয়েছে৷',
    columns: {
      name: 'নাম',
      type: 'টাইপ',
      identity: 'পরিচয়',
      endpoint: 'শেষবিন্দু',
      enabled: 'সক্রিয়',
      connected: 'সংযুক্ত',
      lastEvent: 'শেষ ঘটনা',
      actions: 'কর্ম'
    },
    state: {
      connected: 'সংযুক্ত',
      disconnected: 'সংযোগ বিচ্ছিন্ন'
    },
    actions: {
      refresh: 'রিফ্রেশ',
      add: 'চ্যানেল যোগ করুন',
      cancel: 'বাতিল করুন',
      create: 'তৈরি করুন',
      save: 'সংরক্ষণ করুন',
      confirmDelete: 'চ্যানেল "{name}" মুছবেন? এটি পূর্বাবস্থায় ফেরানো যাবে না।'
    },
    form: {
      createTitle: 'চ্যানেল যোগ করুন',
      editTitle: '"{name}" সম্পাদনা করুন',
      name: 'চ্যানেলের নাম',
      type: 'চ্যানেলের ধরন',
      agent: 'এজেন্ট',
      minResponseInterval: 'ন্যূনতম প্রতিক্রিয়া ব্যবধান (সেকেন্ড)',
      allowInterjection: 'ক্ষেপণের অনুমতি দিন',
      irc: {
        host: 'আইআরসি সার্ভার',
        port: 'বন্দর',
        serverPassword: 'সার্ভার পাসওয়ার্ড',
        nick: 'ডাকনাম',
        username: 'ব্যবহারকারীর নাম',
        realname: 'আসল নাম',
        channels: 'চ্যানেল',
        channelsHint: 'প্রতি লাইনে একটি। বিন্যাস: #চ্যানেল [কী]',
        agent: 'এজেন্ট',
        respondToChannel: 'চ্যানেল বার্তা সাড়া',
        respondToDm: 'সরাসরি বার্তাগুলিতে সাড়া দিন',
        respondToNotices: 'নোটিশের জবাব দিন',
        allowedDmUsers: 'ডিএম ব্যবহারকারীদের অনুমতি দেওয়া হয়েছে',
        reconnect: 'স্বতঃ-পুনঃসংযোগ'
      },
      telegram: {
        botToken: 'বট টোকেন',
        tokenHint: 'বিদ্যমান টোকেন রাখতে ফাঁকা ছেড়ে দিন'
      },
      vk: {
        accessToken: 'কমিউনিটি অ্যাক্সেস টোকেন',
        groupId: 'গ্রুপ আইডি'
      },
      bluesky: {
        handle: 'হ্যান্ডেল',
        appPassword: 'অ্যাপ পাসওয়ার্ড',
        pds: 'PDS URL'
      },
      mastodon: {
        handle: 'হ্যান্ডেল',
        instance: 'ইনস্ট্যান্স URL'
      },
      email: {
        apiKey: 'AgentMail API কী',
        apiKeyHint: 'বর্তমান কী রাখতে খালি ছেড়ে দিন',
        inboxId: 'ইনবক্স আইডি',
        pollingWait: 'ভোটের ব্যবধান (সেকেন্ড)'
      },
      twitter: {
        tier: 'API স্তর',
        clientId: 'OAuth ক্লায়েন্ট আইডি',
        clientSecret: 'OAuth ক্লায়েন্ট সিক্রেট',
        accessToken: 'অ্যাক্সেস টোকেন',
        tokenHint: 'বিদ্যমান টোকেন রাখতে ফাঁকা ছেড়ে দিন',
        refreshToken: 'টোকেন রিফ্রেশ করুন',
        authorize: 'টুইটার দিয়ে অনুমোদন করুন',
        oauthStarted: 'অনুমোদন উইন্ডো খোলা হয়েছে। আপনার ব্রাউজারে প্রবাহ সম্পূর্ণ করুন.'
      },
      discord: {
        botToken: 'বট টোকেন',
        tokenHint: 'বিদ্যমান টোকেন রাখতে ফাঁকা ছেড়ে দিন',
        applicationId: 'অ্যাপ্লিকেশন আইডি (স্ল্যাশ কমান্ডের জন্য)',
        monitoredChannels: 'ট্র্যাক করা চ্যানেল',
        monitoredChannelsHint: 'প্রসঙ্গের জন্য ট্র্যাক করার চ্যানেল আইডি (প্রতি লাইনে একটি)। বার্তাগুলি প্রতিক্রিয়া ট্রিগার না করে লগ করা হয়।',
        respondToDm: 'DMs কে সাড়া দিন',
        respondToMentions: 'উল্লেখ উত্তর',
        monitorAllChannels: 'সব চ্যানেল ট্র্যাক করুন',
        respondToChannels: 'ট্র্যাক করা চ্যানেলে যেকোনো বার্তার উত্তর দিন',
        dmWhitelistEnabled: 'অনুমোদিত ব্যবহারকারীদের জন্য DM সীমাবদ্ধ করুন',
        allowedDmUsers: 'অনুমোদিত DM ব্যবহারকারী',
        allowedDmUsersHint: 'Discord ব্যবহারকারী নাম (প্রতি লাইনে একটি)। শুধুমাত্র এই ব্যবহারকারীরা বটকে DM পাঠাতে পারেন।'
      },
      slack: {
        botToken: 'বট টোকেন (xoxb-)',
        tokenHint: 'বিদ্যমান টোকেন রাখতে ফাঁকা ছেড়ে দিন',
        appToken: 'অ্যাপ টোকেন (xapp-)',
        appTokenHint: 'সকেট মোডের জন্য (ফেজ 2)। ফেজ 1 এর জন্য ঐচ্ছিক।',
        monitoredChannels: 'পর্যবেক্ষণ করা চ্যানেল (ওভাররাইড)',
        monitoredChannelsHint: 'বট অটো-মনিটর করে যে সমস্ত চ্যানেল এটির সদস্য। সুযোগ সীমাবদ্ধ করতে এখানে শুধুমাত্র আইডি যোগ করুন।',
        respondToMentions: "{'@'}উল্লেখের উত্তর দিন",
        respondToAll: 'সমস্ত বার্তার উত্তর দিন (উল্লেখ ফিল্টার উপেক্ষা করে)',
        threadedReplies: 'চ্যানেলগুলিতে থ্রেড উত্তর (প্রতিটি বার্তা একটি থ্রেড শুরু করে)'
      }
    }
  } as const;
