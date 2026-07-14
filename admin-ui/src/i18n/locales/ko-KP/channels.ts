export const channels = {
    title: '채널',
    empty: '구성된 채널이 없습니다. 시작하려면 하나를 추가하세요.',
    createSuccess: '채널 "{name}"이(가) 성공적으로 생성되었습니다.',
    updateSuccess: '채널 \'{name}\'이(가) 성공적으로 업데이트되었습니다.',
    deleteSuccess: '\'{name}\' 채널이 삭제되었습니다.',
    columns: {
      name: '이름',
      type: '유형',
      identity: '아이덴티티',
      endpoint: '엔드포인트',
      enabled: '활성화됨',
      connected: '연결됨',
      lastEvent: '마지막 이벤트',
      actions: '작업'
    },
    state: {
      connected: '연결됨',
      disconnected: '연결이 끊김'
    },
    actions: {
      refresh: '새로고침',
      add: '채널 추가',
      cancel: '취소',
      create: '만들기',
      save: '저장',
      confirmDelete: '\'{name}\' 채널을 삭제하시겠습니까? 이 작업은 취소할 수 없습니다.'
    },
    form: {
      createTitle: '채널 추가',
      editTitle: '"{name}" 편집',
      name: '채널 이름',
      type: '채널 유형',
      agent: '대리인',
      minResponseInterval: '최소 응답 간격 (초)',
      allowInterjection: '삽입 허용',
      irc: {
        host: 'IRC 서버',
        port: '항구',
        serverPassword: '서버 비밀번호',
        nick: '닉네임',
        username: '사용자 이름',
        realname: '실명',
        channels: '채널',
        channelsHint: '한 줄에 하나씩. 형식: #channel [키]',
        agent: '대리인',
        respondToChannel: '채널 메시지에 응답',
        respondToDm: '다이렉트 메시지에 응답하기',
        respondToNotices: '통지에 응답',
        allowedDmUsers: '허용된 DM 사용자',
        reconnect: '자동 재연결'
      },
      telegram: {
        botToken: '봇 토큰',
        tokenHint: '기존 토큰을 유지하려면 비워 두세요.'
      },
      vk: {
        accessToken: '커뮤니티 액세스 토큰',
        groupId: '그룹 ID'
      },
      bluesky: {
        handle: '손잡이',
        appPassword: '앱 비밀번호',
        pds: 'PDS URL'
      },
      mastodon: {
        handle: '손잡이',
        instance: '인스턴스 URL'
      },
      email: {
        apiKey: 'AgentMail API 키',
        apiKeyHint: '현재 키를 유지하려면 비워 두세요.',
        inboxId: '받은 편지함 ID',
        pollingWait: '폴링 간격(초)'
      },
      twitter: {
        tier: 'API 계층',
        clientId: 'OAuth 클라이언트 ID',
        clientSecret: 'OAuth 클라이언트 비밀번호',
        accessToken: '액세스 토큰',
        tokenHint: '기존 토큰을 유지하려면 비워 두세요.',
        refreshToken: '새로고침 토큰',
        authorize: '트위터로 승인하기',
        oauthStarted: '승인창이 열렸습니다. 브라우저에서 흐름을 완료하세요.'
      },
      discord: {
        botToken: '봇 토큰',
        tokenHint: '기존 토큰을 유지하려면 비워 두세요.',
        applicationId: '애플리케이션 ID(슬래시 명령용)',
        monitoredChannels: '모니터링 채널',
        monitoredChannelsHint: '한 줄에 하나의 채널 ID입니다. 봇이 서버에 있어야 합니다.',
        respondToDm: 'DM에 응답하기',
        respondToMentions: '멘션에 응답'
      },
      slack: {
        botToken: '봇 토큰(xoxb-)',
        tokenHint: '기존 토큰을 유지하려면 비워 두세요.',
        appToken: '앱 토큰(xapp-)',
        appTokenHint: '소켓 모드(2단계)의 경우. 1단계에서는 선택 사항입니다.',
        monitoredChannels: '모니터링되는 채널(재정의)',
        monitoredChannelsHint: 'Bot은 자신이 속한 모든 채널을 자동으로 모니터링합니다. 범위를 제한하려면 여기에 ID만 추가하세요.',
        respondToMentions: "{'@'}멘션에 응답",
        respondToAll: '모든 메시지에 응답(멘션 필터 무시)',
        threadedReplies: '스레드는 채널에서 응답합니다(각 메시지는 스레드를 시작합니다).'
      }
    }
  } as const;
