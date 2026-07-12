export const interfaces = {
    title: '접속면 관리',
    actions: {
      refresh: '갱신',
      add: '접속면 추가',
      edit: '수정',
      enable: '사용',
      disable: '사용 중지',
      delete: '삭제',
      confirmDelete: '접속면 "{name}"를 삭제하겠습니까?'
    },
    columns: {
      name: '이름',
      type: '형식',
      module: '모듈 ID',
      enabled: '사용중',
      connected: '련결됨',
      lastEvent: '마지막 사건',
      actions: '동작'
    },
    state: {
      enabled: '예',
      disabled: '아니오',
      connected: '련결됨',
      disconnected: '련결 끊김'
    },
    empty: '아직 설정된 접속면이 없습니다.',
    form: {
      createTitle: '접속면 작성',
      editTitle: '접속면 수정: {name}',
      name: '실례 이름',
      type: '접속면 형식',
      moduleId: '모듈 ID',
      enabled: '사용중',
      configJson: '설정 JSON',
      create: '작성',
      save: '저장',
      cancel: '취소',
      irc: {
        host: '호스트',
        port: '포트',
        nick: '별명',
        serverPassword: '서버 암호',
        username: '사용자 이름',
        realname: '실제 이름',
        dmOnly: '직접소식 전용 방식',
        respondToChannel: '통로 활동에 응답',
        respondToDm: '직접소식에 응답',
        respondToNotices: '통지에 응답',
        allowedDmUsers: '허용된 직접소식 사용자',
        allowedDmUsersHint: '선택적 허용목록; 반점 또는 새 줄로 구분.',
        agentId: '에이전트 ID',
        reconnectEnabled: '재련결 사용',
        reconnectInitialDelay: '재련결 초기 지연 (ms)',
        reconnectMaxDelay: '재련결 최대 지연 (ms)'
      }
    },
    createSuccess: '접속면 "{name}"가 작성되었습니다.',
    updateSuccess: '접속면 "{name}"가 갱신되었습니다.',
    deleteSuccess: '접속면 "{name}"가 삭제되었습니다.'
  } as const;
