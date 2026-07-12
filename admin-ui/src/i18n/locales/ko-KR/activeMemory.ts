export const activeMemory = {
    title: '활성 메모리',
    subtitle: '조립된 에이전트 컨텍스트 — 에이전트가 서문에서 보는 것',
    empty: '어셈블된 컨텍스트를 보려면 에이전트를 선택하세요.',
    actions: {
      refresh: '새로고침'
    },
    labels: {
      agent: '대리인',
      session: '세션',
      syntheticSession: '합성(테스트를 위한 빈 세션)',
      blocks: '블록'
    },
    flags: {
      synthetic: '합성 세션',
      live: '라이브 세션'
    },
    sections: {
      rendered: '렌더링된 출력',
      blocks: '블록 분해'
    }
  } as const;
