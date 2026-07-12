export const ontology = {
    title: '本体探索者',
    subtitle: '浏览语义实体及其观察支持的属性。',
    actions: {
      refresh: '刷新'
    },
    sections: {
      agent: '代理',
      categories: '类别',
      entities: '实体',
      details: '实体详情',
      properties: '属性'
    },
    states: {
      new: '新的',
      current: '当前',
      deprecated: '已弃用'
    },
    empty: {
      entities: '没有找到该类别的实体。',
      details: '选择一个实体来检查属性。'
    }
  } as const;
