export const chat = {
    sessionLabel: '세션',
    newSession: '새 세션',
    refresh: '새로고침',
    stopGenerating: '생성 중지',
    emptyTitle: '대화 시작',
    emptyDescription: 'Animus에게 메모리, 구성 또는 라이브 세션에 대해 물어보세요.',
    streamState: {
      streaming: '스트리밍...',
      stopped: '중지됨'
    },
    jumpToLatest: '최신으로 이동',
    composerPlaceholder: '애니머스에게 메시지 보내기...',
    adminTokenLabel: '관리자 토큰(선택사항)',
    send: '보내기',
    contextTitle: '맥락',
    context: {
      session: '세션',
      layers: '레이어',
      tools: '도구',
      budget: '예산',
      fallbackNote: '세션이 변경되면 컨텍스트 스냅샷이 업데이트됩니다.'
    },
    sidebarTabs: {
      context: '맥락',
      sessions: '세션',
      messages: '메시지',
      noSessions: '아직 세션이 없습니다.',
      searchSessions: 'Search sessions...',
      newSessionHint: '새 스레드를 시작하려면 메시지를 작성하세요.'
    },
    thinking: {
      label: "생각"
    },
    toolResult: {
      label: "도구 결과"
    },
    toolCall: {
      label: "도구 호출"
    },
    reasoning: {
      label: '추론 모드',
      enabled: '켜기',
      disabled: '끄기',
      effort: '노력',
      instructionLabel: '시스템 지시',
      instructionPlaceholder: '사용자 정의 추론 지침(비어 있으면 기본값 사용)',
      notSupported: '이 모델에서는 추론을 사용할 수 없습니다.'
    },
    provider: {
      label: '공급자',
      select: '공급자',
      model: '모델'
    },
    agent: {
      label: '에이전트(새 세션)'
    },
    status: {
      closed: '휴무',
      connecting: '연결 중',
      open: '연결됨'
    },
    errors: {
      websocket: '웹소켓 오류',
      websocketNotConnected: 'WebSocket이 아직 연결되지 않았습니다. 다시 시도해 주세요.',
      agentNotReady: '에이전트 선택이 아직 로드 중입니다. 잠시 후에 다시 시도해 주세요.',
      unknownWebsocket: '알 수 없는 웹소켓 오류',
      sessionLoadFailed: '세션 {sessionId}을(를) 로드하지 못했습니다: {message}'
    }
  } as const;
