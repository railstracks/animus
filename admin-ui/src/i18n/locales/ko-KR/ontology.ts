export const ontology = {
    title: '온톨로지 탐색기',
    subtitle: '의미론적 엔터티와 관찰 기반 속성을 찾아보세요.',
    actions: {
      refresh: '새로고침'
    },
    sections: {
      agent: '대리인',
      categories: '카테고리',
      entities: '엔터티',
      details: '엔터티 세부정보',
      properties: '속성'
    },
    states: {
      new: '새로운',
      current: '현재',
      deprecated: '더 이상 사용되지 않음'
    },
    empty: {
      entities: '이 카테고리에 해당하는 엔터티가 없습니다.',
      details: '속성을 검사할 엔터티를 선택하세요.'
    }
  } as const;
