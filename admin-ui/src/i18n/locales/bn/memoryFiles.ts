export const memoryFiles = {
    title: 'মেমরি ফাইল',
    subtitle: 'কাঁচা টেক্সট আর্টিফ্যাক্ট সঞ্চয় করুন এবং মেমরি ডোমেন জুড়ে অনুসন্ধান করুন।',
    common: {
      yes: 'হ্যাঁ',
      no: 'না'
    },
    actions: {
      refresh: 'রিফ্রেশ',
      open: 'খোলা',
      delete: 'মুছুন',
      process: 'প্রক্রিয়াকরণের জন্য সারি',
      import: 'আমদানি',
      importBatch: 'ব্যাচ আমদানি করুন',
      saveMetadata: 'মেটাডেটা সংরক্ষণ করুন',
      search: 'অনুসন্ধান করুন'
    },
    fields: {
      sourcePath: 'উৎস পথ',
      fileType: 'ফাইলের ধরন',
      contentMutable: 'বিষয়বস্তু পরিবর্তনযোগ্য',
      agentId: 'এজেন্ট আইডি',
      status: 'স্ট্যাটাস',
      agentFilter: 'এজেন্ট দ্বারা ফিল্টার',
      allAgents: 'সকল এজেন্ট',
      superseded: 'বাতিল করা হয়েছে',
      content: 'বিষয়বস্তু'
    },
    types: {
      all: 'সকল প্রকার',
      expanded_memory: 'প্রসারিত মেমরি',
      session_log: 'সেশন লগ',
      daily_note: 'দৈনিক নোট',
      bootstrap_file: 'বুটস্ট্র্যাপ ফাইল',
      journal: 'জার্নাল',
      external_doc: 'বাহ্যিক ডক'
    },
    stats: {
      title: 'পরিসংখ্যান'
    },
    list: {
      title: 'ফাইল তালিকা',
      typeFilter: 'ফিল্টার টাইপ করুন',
      limit: 'সীমা',
      offset: 'অফসেট',
      columns: {
        id: 'আইডি',
        type: 'টাইপ',
        sourcePath: 'উৎস পথ',
        contentMutable: 'পরিবর্তনযোগ্য',
        agentId: 'এজেন্ট আইডি',
      status: 'স্ট্যাটাস',
      agentFilter: 'এজেন্ট দ্বারা ফিল্টার',
      allAgents: 'সকল এজেন্ট',
        superseded: 'বাতিল করা হয়েছে',
        importedAt: 'আমদানিকৃত',
        actions: 'কর্ম'
      },
      status: {
        unprocessed: 'আনপ্রসেসড',
        processed: 'প্রক্রিয়াকৃত'
      }
    },
    import: {
      singleTitle: 'ফাইল আমদানি করুন',
      selectFile: 'ফাইল নির্বাচন করুন',
      selected: 'নির্বাচিত',
      batchTitle: 'ব্যাচ আমদানি',
      selectFiles: 'ফাইল নির্বাচন করুন',
      filesSelected: 'ফাইল নির্বাচিত'
    },
    detail: {
      title: 'ফাইল বিস্তারিত',
      empty: 'মেটাডেটা পরিদর্শন এবং সম্পাদনা করতে তালিকা থেকে একটি ফাইল নির্বাচন করুন৷',
      createdAt: 'তৈরি হয়েছে',
      importedAt: 'আমদানিকৃত',
      contentTitle: 'বিষয়বস্তু',
      contentImmutableNotice: 'বিষয়বস্তু সম্পাদনা অক্ষম করা হয়েছে কারণ এই ফাইলটিকে অপরিবর্তনীয় হিসাবে চিহ্নিত করা হয়েছে৷'
    },
    search: {
      title: 'ইউনিফাইড সার্চ',
      query: 'অনুসন্ধান ক্যোয়ারী',
      limit: 'সীমা',
      relevance: 'প্রাসঙ্গিকতা',
      empty: 'এখনও কোন অনুসন্ধান ফলাফল.',
      domains: {
        observation: 'পর্যবেক্ষণ',
        observations: 'পর্যবেক্ষণ',
        ontology: 'অন্টোলজি',
        memory_file: 'ফাইল',
        sessions: 'সেশন'
      }
    },
    errors: {
      loadFiles: 'মেমরি ফাইল লোড করতে ব্যর্থ.',
      loadDetail: 'ফাইলের বিশদ বিবরণ লোড করতে ব্যর্থ হয়েছে৷',
      importRequired: 'আমদানি করার জন্য একটি ফাইল নির্বাচন করুন৷',
      importSingle: 'মেমরি ফাইল আমদানি করতে ব্যর্থ হয়েছে.',
      batchRequired: 'ব্যাচ আমদানির জন্য কমপক্ষে একটি ফাইল নির্বাচন করুন৷',
      importBatch: 'ব্যাচ পেলোড আমদানি করতে ব্যর্থ হয়েছে৷',
      updateMetadata: 'মেটাডেটা আপডেট করতে ব্যর্থ হয়েছে৷',
      delete: 'মেমরি ফাইল মুছে ফেলতে ব্যর্থ হয়েছে.',
      search: 'মেমরি অনুসন্ধান ব্যর্থ হয়েছে.'
    }
  } as const;
