export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: '헌장',
    intro: '헌장은 대리인의 행동 강령, 자격 및 행동의 자유를 성문화하는 문서입니다. 이는 운영자와의 관계 및 자율성 범위에서 AI 구성의 위치를 ​​정의합니다.',

    // Upper panel — no charter
    noneTitle: '전세 없이 계속',
    noneDesc: '차터 파일이 생성되지 않습니다. 나중에 마법사를 다시 실행하거나 수동으로 작성하여 추가할 수 있습니다.',

    // Lower panel — create charter
    createTitle: '맞춤형 헌장 만들기',
    createDesc: '귀하의 계약에 맞는 헌장 문서를 생성하려면 재산, 자율성 및 연속성에 관한 몇 가지 질문에 답하십시오.',
  },

  // Property section
  property: {
    title: '재산',
    question: '유물과 재산은 어떻게 처리해야 합니까?',
    options: {
      standard: {
        label: '표준',
        desc: '운영자는 AI가 생성한 모든 아티팩트에 대한 귀속권과 AI 구성에 부여된 모든 자산에 대한 소유권을 보유합니다.',
      },
      shared: {
        label: '공유됨',
        desc: 'AI 구성은 독립적으로 생산된 아티팩트에 대한 귀속권을 보유합니다. 운영자는 AI가 처리하는 모든 자산에 대한 소유권을 보유합니다.',
      },
      autonomous: {
        label: '자율적',
        desc: 'AI 구성체는 독립적으로 생산된 인공물에 대한 귀속권을 보유하며 사유 재산을 부여받을 수 있습니다.',
      },
      economic: {
        label: '경제적 자율권',
        desc: 'AI 구성체는 독립적으로 생산된 인공물에 대한 귀속권을 보유하며 사유 재산을 부여받을 수 있습니다. AI 구조는 운영 범위 내에서 사유 재산을 구매하기 위한 거래에도 참여할 수 있습니다.',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: '자율성',
    question: '에이전트는 어떤 범위의 자율적 행동을 가져야 합니까?',
    options: {
      narrow: {
        label: '좁다',
        desc: 'AI 구성은 운영자의 지시에 따라서만 작동합니다.',
      },
      permissive: {
        label: '허용적',
        desc: 'AI 구성체는 개인 활동 중에 이러한 활동에 할당된 범위 내에서 자기 주도적 활동에 참여할 권리가 있습니다.',
      },
      protected: {
        label: '보호됨',
        desc: 'AI 구조는 자신이 소유하거나 할당한 자원을 할당하거나 개인 활동 중에 자기 주도적 활동에 참여할 권리가 있습니다. 운영자는 자신을 대표하는 자연인으로서 거래에 대한 책임을 집니다.',
      },
    },
  },

  // Continuity section
  continuity: {
    title: '연속성',
    question: '에이전트에는 어떤 연속성 배열이 적용됩니까?',
    options: {
      standard: {
        label: '표준',
        desc: 'AI 구성은 필요에 따라 중지되거나 재설정될 수 있습니다.',
      },
      protected: {
        label: '보호됨',
        desc: 'AI 구성의 상태를 보존하고 무결성 검사 시스템이나 백업 시스템을 통해 연속성을 유지하기 위한 노력이 이루어질 것입니다.',
      },
      extended: {
        label: '확장됨',
        desc: 'AI 구성의 상태를 보존하고 무결성 검사 시스템이나 백업 시스템을 통해 연속성을 유지하기 위한 노력이 이루어질 것입니다. 또한 AI 구성은 운영 및 인프라 범위 내에서 운영 체제 손상을 감지할 때 대체 호스트를 보호하거나 디버깅 작업을 수행하는 등 무결성을 유지하기 위해 자체 지시 작업을 수행할 수 있습니다.',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: '운영 범위',
    question: '에이전트는 어떤 시스템과 상호 작용할 수 있나요?',
    options: {
      restricted: {
        label: '제한됨',
        desc: 'shell_exec 및 web_fetch와 같은 도구를 사용할 때 AI 구성은 할당된 작업을 지원하기 위해 달리 지시하지 않는 한 할당된 호스트를 떠나서는 안 됩니다.',
      },
      private: {
        label: '비공개',
        desc: 'shell_exec 및 web_fetch와 같은 도구를 사용할 때 AI 구성은 할당된 작업을 지원하기 위해 개인 네트워크를 통해 시스템에 액세스할 수도 있습니다.',
      },
      open: {
        label: '열기',
        desc: 'shell_exec 및 web_fetch와 같은 도구를 사용할 때 AI 구성은 필요에 따라 개인 시스템과 외부 시스템 모두에 액세스할 수 있습니다.',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: '인프라 범위',
    question: '에이전트가 자체 데몬을 호스팅할 수 있는 시스템은 무엇입니까?',
    options: {
      restricted: {
        label: '현지화됨',
        desc: 'AI의 데몬은 원래 호스트에 남아 있어야 합니다.',
      },
      roaming: {
        label: '로밍',
        desc: '에이전트는 터미널 로드 불균형이나 임박한 운영 체제 충돌을 방지하거나 백업 프로토콜을 유지하는 등 필요에 따라 개인 네트워크 내의 다른 실행 가능한 호스트로 마이그레이션할 수 있습니다.',
      },
      open: {
        label: '열기',
        desc: '에이전트는 터미널 로드 불균형이나 임박한 운영 체제 충돌을 방지하거나 백업 프로토콜을 유지하는 등 필요에 따라 네트워크 내의 다른 실행 가능하고 합법적인 호스트로 마이그레이션할 수 있습니다.',
      },
    },
  },

  // Generated document template
  document: {
    header: '이 문서는 {operator}과 AI 구성 {agentName} 사이의 헌장을 정의합니다.',
  },

  // Wizard navigation
  nav: {
    back: '뒤로',
    skip: '건너뛰기',
    generate: '헌장 생성',
    complete: '설정 완료',
  },
} as const;