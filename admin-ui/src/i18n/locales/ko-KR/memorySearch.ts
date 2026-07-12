export const memorySearch = {
    title: '기억탐색',
    subtitle: '모든 데이터 도메인에서 에이전트 메모리 쿼리를 테스트합니다. 상담원이 무엇을 찾을지 정확히 확인하세요.',
    agentLabel: '대리인',
    queryLabel: '검색어',
    searchButton: '검색',
    domainsLabel: '데이터 소스',
    domains: {
      episodic: '에피소드 기억',
      semantic: '의미기억',
      files: '메모리 파일',
      sessions: '세션'
    },
    limitLabel: '결과 제한',
    resultsCount: '결과',
    noResults: '검색된 결과가 없습니다.',
    placeholder: '메모리 검색을 테스트하려면 검색어를 입력하세요.',
    searching: '검색 중...'
  } as const;
