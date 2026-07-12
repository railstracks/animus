export const ontology = {
    title: '本體探索者',
    subtitle: '瀏覽語意實體及其觀察所支援的屬性。',
    actions: {
      refresh: '重新整理'
    },
    sections: {
      agent: '代理人',
      categories: '類別',
      entities: '實體',
      details: '實體詳情',
      properties: '特性'
    },
    states: {
      new: '新的',
      current: '目前的',
      deprecated: '已棄用'
    },
    empty: {
      entities: '沒有找到該類別的實體。',
      details: '選擇一個實體來檢查屬性。'
    }
  } as const;
