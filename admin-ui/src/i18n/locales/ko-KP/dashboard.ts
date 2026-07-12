export const dashboard = {
  // First-run banner
  banner: {
    welcome: '애니머스에 오신 것을 환영합니다.',
    welcomeSub: '시작하려면 첫 번째 에이전트를 설정하세요.',
    beginSetup: '설정 시작 →',
  },
  // Status ribbon
  ribbon: {
    uptime: '가동 시간',
    agents: '에이전트',
    sessions: '세션',
    providers: '공급자',
    errors: '실수',
  },
  // Recent Sessions card
  recentSessions: {
    title: '최근 세션',
    viewAll: '모두 보기',
    empty: '아직 세션이 없습니다.',
    startChat: '채팅 시작 →',
    messages: '메시지',
  },
  // Memory card
  memory: {
    title: '메모리',
    inspect: '검사',
    empty: '구성된 메모리 레이어가 없습니다.',
    totalObservations: '총 관측치',
  },
  // Provider card
  providers: {
    title: '공급자',
    manage: '관리하다',
    empty: '구성된 제공업체가 없습니다.',
    runWizard: '설정 마법사 실행 →',
  },
  // Quick Actions card
  quickActions: {
    title: '빠른 작업',
    newChat: '새 채팅',
    addAgent: '에이전트 추가',
    provider: '공급자',
    logs: '로그',
    scheduler: '스케줄러',
    memory: '메모리',
  },
  // Daemon Info card
  daemonInfo: {
    title: '데몬 정보',
    status: '상태',
    uptime: '가동 시간',
    agents: '에이전트',
    activeSessions: '활성 세션',
    providers: '공급자',
  },
  // Memory Layers card
  memoryLayers: {
    title: '메모리 레이어',
    empty: '구성된 메모리 레이어가 없습니다.',
  },
  // State labels
  state: {
    loading: '로드 중',
    unknown: '알 수 없음',
  },
  // Error messages
  errors: {
    sessions: '세션을 로드하지 못했습니다.',
    providers: '공급자를 로드하지 못했습니다.',
    memory: '메모리 API를 사용할 수 없습니다',
  },
} as const;