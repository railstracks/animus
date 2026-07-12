export const ontology = {
    title: 'ओन्टोलॉजी एक्सप्लोरर',
    subtitle: 'सिमेंटिक इकाइयां और उनके अवलोकन-समर्थित गुण ब्राउज़ करें।',
    actions: {
      refresh: 'ताज़ा करें'
    },
    sections: {
      agent: 'एजेंट',
      categories: 'श्रेणियाँ',
      entities: 'संस्थाएँ',
      details: 'इकाई विवरण',
      properties: 'गुण'
    },
    states: {
      new: 'नया',
      current: 'वर्तमान',
      deprecated: 'बहिष्कृत'
    },
    empty: {
      entities: 'इस श्रेणी के लिए कोई इकाई नहीं मिली.',
      details: 'संपत्तियों का निरीक्षण करने के लिए एक इकाई का चयन करें।'
    }
  } as const;
