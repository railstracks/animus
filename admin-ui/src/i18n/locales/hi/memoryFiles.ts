export const memoryFiles = {
    title: 'मेमोरी फ़ाइलें',
    subtitle: 'कच्चे पाठ कलाकृतियों को संग्रहीत करें और मेमोरी डोमेन में खोजें।',
    common: {
      yes: 'हाँ',
      no: 'नहीं'
    },
    actions: {
      refresh: 'ताज़ा करें',
      open: 'खुला',
      delete: 'हटाएँ',
      process: 'प्रसंस्करण के लिए कतार',
      import: 'आयात करें',
      importBatch: 'आयात बैच',
      saveMetadata: 'मेटाडेटा सहेजें',
      search: 'खोजें'
    },
    fields: {
      sourcePath: 'स्रोत पथ',
      fileType: 'फ़ाइल प्रकार',
      contentMutable: 'सामग्री परिवर्तनशील',
      agentId: 'एजेंट आईडी',
      status: 'स्थिति',
      agentFilter: 'एजेंट द्वारा फ़िल्टर करें',
      allAgents: 'सभी एजेंट',
      superseded: 'अधिक्रमण किया हुआ',
      content: 'सामग्री'
    },
    types: {
      all: 'सभी प्रकार',
      expanded_memory: 'विस्तारित मेमोरी',
      session_log: 'सत्र लॉग',
      daily_note: 'दैनिक नोट',
      bootstrap_file: 'बूटस्ट्रैप फ़ाइल',
      journal: 'जर्नल',
      external_doc: 'बाहरी दस्तावेज़'
    },
    stats: {
      title: 'आँकड़े'
    },
    list: {
      title: 'फ़ाइल सूची',
      typeFilter: 'फ़िल्टर टाइप करें',
      limit: 'सीमा',
      offset: 'ऑफसेट',
      columns: {
        id: 'आईडी',
        type: 'प्रकार',
        sourcePath: 'स्रोत पथ',
        contentMutable: 'परिवर्तनशील',
        agentId: 'एजेंट आईडी',
      status: 'स्थिति',
      agentFilter: 'एजेंट द्वारा फ़िल्टर करें',
      allAgents: 'सभी एजेंट',
        superseded: 'अधिक्रमण किया हुआ',
        importedAt: 'आयातित',
        actions: 'क्रियाएँ'
      },
      status: {
        unprocessed: 'असंसाधित',
        processed: 'संसाधित'
      }
    },
    import: {
      singleTitle: 'फ़ाइल आयात करें',
      selectFile: 'फ़ाइल का चयन करें',
      selected: 'चयनित',
      batchTitle: 'बैच आयात',
      selectFiles: 'फ़ाइलें चुनें',
      filesSelected: 'फ़ाइलें चयनित'
    },
    detail: {
      title: 'फ़ाइल विवरण',
      empty: 'मेटाडेटा का निरीक्षण और संपादन करने के लिए सूची से एक फ़ाइल का चयन करें।',
      createdAt: 'बनाया गया',
      importedAt: 'आयातित',
      contentTitle: 'सामग्री',
      contentImmutableNotice: 'सामग्री संपादन अक्षम है क्योंकि यह फ़ाइल अपरिवर्तनीय के रूप में चिह्नित है।'
    },
    search: {
      title: 'एकीकृत खोज',
      query: 'खोज क्वेरी',
      limit: 'सीमा',
      relevance: 'प्रासंगिकता',
      empty: 'अभी तक कोई खोज परिणाम नहीं.',
      domains: {
        observation: 'अवलोकन',
        observations: 'टिप्पणियाँ',
        ontology: 'ओन्टोलॉजी',
        memory_file: 'फ़ाइलें',
        sessions: 'सत्र'
      }
    },
    errors: {
      loadFiles: 'मेमोरी फ़ाइलें लोड करने में विफल.',
      loadDetail: 'फ़ाइल विवरण लोड करने में विफल.',
      importRequired: 'आयात करने के लिए एक फ़ाइल का चयन करें.',
      importSingle: 'मेमोरी फ़ाइल आयात करने में विफल.',
      batchRequired: 'बैच आयात के लिए कम से कम एक फ़ाइल का चयन करें.',
      importBatch: 'बैच पेलोड आयात करने में विफल.',
      updateMetadata: 'मेटाडेटा अपडेट करने में विफल.',
      delete: 'मेमोरी फ़ाइल हटाने में विफल.',
      search: 'स्मृति खोज विफल रही.'
    }
  } as const;
