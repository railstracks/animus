export const memory = {
    title: '메모리 브라우저',
    actions: {
      refresh: '새로고침',
      triggerConsolidation: '통합 실행'
    },
    common: {
      notAvailable: '해당사항 없음'
    },
    layers: {
      title: '레이어',
      empty: '메모리 레이어를 찾을 수 없습니다.',
      horizon: '지평선',
      consolidationInterval: '통합 간격',
      retentionPolicy: '보존 정책',
      perspectiveCurrent: '현재의 관점',
      perspectivePast: '과거의 관점',
      perspectiveFuture: '미래의 관점',
      viewObservations: '관찰 보기'
    },
    consolidation: {
      title: '통합',
      activeJob: '활성 직업',
      lastRun: '마지막 실행',
      lastJob: '마지막 직업',
      promoted: '승격됨',
      demoted: '강등됨',
      archived: '보관됨',
      state: {
        running: '달리기',
        idle: '유휴'
      }
    },
    list: {
      title: '관찰',
      activeLayer: '레이어: {layer}',
      sortBy: '정렬',
      orderBy: '주문',
      tagFilter: '태그로 필터링',
      pageSize: '페이지 크기',
      compactMode: '콤팩트',
      openDetail: '세부정보',
      emptyLayer: '이 레이어에는 아직 관측 결과가 없습니다.',
      emptyFilter: '현재 태그 필터와 일치하는 관찰이 없습니다.',
      sort: {
        weight: '무게',
        age: '나이',
        tags: '태그'
      },
      order: {
        desc: '내림차순',
        asc: '오름차순'
      },
      columns: {
        content: '내용',
        tags: '태그',
        weight: '무게',
        age: '나이',
        decay: '부패',
        actions: '작업'
      }
    },
    detail: {
      title: '관찰 세부정보',
      empty: '검사하고 편집할 관찰을 선택합니다.',
      id: '아이디',
      content: '내용',
      layer: '레이어',

      timestamp: '타임스탬프',
      demotionReason: '강등 이유',
      demotionTimestamp: '강등 타임스탬프',
      editWeight: '무게',
      editTags: '태그',
      save: '저장',
      reset: '재설정',
      archive: '아카이브',
      saveSuccess: '관찰이 업데이트되었습니다.',
      archiveSuccess: '관찰 내용이 보관되었습니다.'
    }
  } as const;
