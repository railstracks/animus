export const memoryFiles = {
    title: '메모리 파일',
    subtitle: '원시 텍스트 아티팩트를 저장하고 메모리 도메인 전체를 검색합니다.',
    common: {
      yes: '응',
      no: '아니'
    },
    actions: {
      refresh: '새로고침',
      open: '열기',
      delete: '삭제',
      process: '처리 대기열',
      import: '가져오기',
      importBatch: '일괄 가져오기',
      saveMetadata: '메타데이터 저장',
      search: '검색'
    },
    fields: {
      sourcePath: '소스 경로',
      fileType: '파일 유형',
      contentMutable: '콘텐츠 변경 가능',
      agentId: '에이전트 ID',
      status: '상태',
      agentFilter: '에이전트로 필터링',
      allAgents: '모든 에이전트',
      superseded: '대체됨',
      content: '내용'
    },
    types: {
      all: '모든 유형',
      expanded_memory: '확장 메모리',
      session_log: '세션 로그',
      daily_note: '일일 메모',
      bootstrap_file: '부트스트랩 파일',
      journal: '저널',
      external_doc: '외부 문서'
    },
    stats: {
      title: '통계'
    },
    list: {
      title: '파일 목록',
      typeFilter: '유형 필터',
      limit: '한도',
      offset: '오프셋',
      columns: {
        id: '아이디',
        type: '유형',
        sourcePath: '소스 경로',
        contentMutable: '변경 가능',
        agentId: '에이전트 ID',
      status: '상태',
      agentFilter: '에이전트로 필터링',
      allAgents: '모든 에이전트',
        superseded: '대체됨',
        importedAt: '수입됨',
        actions: '작업'
      },
      status: {
        unprocessed: '처리되지 않음',
        processed: '처리됨'
      }
    },
    import: {
      singleTitle: '파일 가져오기',
      selectFile: '파일 선택',
      selected: '선택됨',
      batchTitle: '일괄 가져오기',
      selectFiles: '파일 선택',
      filesSelected: '선택한 파일'
    },
    detail: {
      title: '파일 세부정보',
      empty: '메타데이터를 검사하고 편집하려면 목록에서 파일을 선택하세요.',
      createdAt: '생성됨',
      importedAt: '수입됨',
      contentTitle: '내용',
      contentImmutableNotice: '이 파일은 변경할 수 없는 것으로 표시되어 있으므로 콘텐츠를 수정할 수 없습니다.'
    },
    search: {
      title: '통합검색',
      query: '검색어',
      limit: '한도',
      relevance: '관련성',
      empty: '아직 검색결과가 없습니다.',
      domains: {
        observation: '관찰',
        observations: '관찰',
        ontology: '온톨로지',
        memory_file: '파일',
        sessions: '세션'
      }
    },
    errors: {
      loadFiles: '메모리 파일을 로드하지 못했습니다.',
      loadDetail: '파일 세부정보를 로드하지 못했습니다.',
      importRequired: '가져올 파일을 선택하세요.',
      importSingle: '메모리 파일을 가져오지 못했습니다.',
      batchRequired: '일괄 가져올 파일을 하나 이상 선택하세요.',
      importBatch: '일괄 페이로드를 가져오지 못했습니다.',
      updateMetadata: '메타데이터를 업데이트하지 못했습니다.',
      delete: '메모리 파일을 삭제하지 못했습니다.',
      search: '메모리 검색에 실패했습니다.'
    }
  } as const;
