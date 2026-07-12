export const ontology = {
    title: 'অন্টোলজি এক্সপ্লোরার',
    subtitle: 'শব্দার্থিক সত্তা এবং তাদের পর্যবেক্ষণ-সমর্থিত বৈশিষ্ট্যগুলি ব্রাউজ করুন।',
    actions: {
      refresh: 'রিফ্রেশ'
    },
    sections: {
      agent: 'এজেন্ট',
      categories: 'ক্যাটাগরি',
      entities: 'সত্তা',
      details: 'সত্তার বিবরণ',
      properties: 'বৈশিষ্ট্য'
    },
    states: {
      new: 'নতুন',
      current: 'বর্তমান',
      deprecated: 'অবমূল্যায়ন'
    },
    empty: {
      entities: 'এই বিভাগের জন্য কোনো সত্তা পাওয়া যায়নি।',
      details: 'বৈশিষ্ট্য পরিদর্শন করতে একটি সত্তা নির্বাচন করুন.'
    }
  } as const;
