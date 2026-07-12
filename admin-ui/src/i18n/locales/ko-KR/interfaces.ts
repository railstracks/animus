export const interfaces = {
    title: '인터페이스 관리',
    actions: {
      refresh: '새로고침',
      add: '인터페이스 추가',
      edit: '편집',
      enable: '활성화',
      disable: '비활성화',
      delete: '삭제',
      confirmDelete: '인터페이스 "{name}"을(를) 삭제할까요?'
    },
    columns: {
      name: '이름',
      type: '유형',
      module: '모듈 ID',
      enabled: '활성화됨',
      connected: '연결됨',
      lastEvent: '마지막 이벤트',
      actions: '작업'
    },
    state: {
      enabled: '예',
      disabled: '아니요',
      connected: '연결됨',
      disconnected: '연결 끊김'
    },
    empty: '아직 구성된 인터페이스가 없습니다.',
    form: {
      createTitle: '인터페이스 생성',
      editTitle: '인터페이스 편집: {name}',
      name: '인스턴스 이름',
      type: '인터페이스 유형',
      moduleId: '모듈 ID',
      enabled: '활성화됨',
      configJson: '구성 JSON',
      create: '생성',
      save: '저장',
      cancel: '취소',
      irc: {
        host: '호스트',
        port: '포트',
        nick: '닉네임',
        serverPassword: '서버 비밀번호',
        username: '사용자 이름',
        realname: '실명',
        dmOnly: 'DM 전용 모드',
        respondToChannel: '채널 활동에 응답',
        respondToDm: '다이렉트 메시지에 응답',
        respondToNotices: '알림에 응답',
        allowedDmUsers: '허용된 DM 사용자',
        allowedDmUsersHint: '선택 사항인 허용 목록입니다. 쉼표 또는 줄바꿈으로 구분하세요.',
        agentId: '에이전트 ID',
        reconnectEnabled: '재연결 활성화',
        reconnectInitialDelay: '재연결 초기 지연 시간(ms)',
        reconnectMaxDelay: '재연결 최대 지연 시간(ms)'
      }
    },
    createSuccess: '인터페이스 "{name}"이(가) 생성되었습니다.',
    updateSuccess: '인터페이스 "{name}"이(가) 업데이트되었습니다.',
    deleteSuccess: '인터페이스 "{name}"이(가) 삭제되었습니다.'
  } as const;
