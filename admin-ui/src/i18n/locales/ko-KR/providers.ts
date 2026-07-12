export const providers = {
    title: '공급자 구성',
    actions: {
      refresh: '새로고침',
      add: '공급자 추가',
      test: '테스트',
      edit: '편집',
      delete: '삭제',
      setDefault: '기본값 설정',
      confirmDelete: '\'{id}\' 제공업체를 삭제하시겠습니까? 이 작업은 취소할 수 없습니다.'
    },
    columns: {
      status: '상태',
      provider: '이름',
      type: '유형',
      baseUrl: '기본 URL',
      defaultModel: '모델',
      default: '기본값',
      actions: '작업'
    },
    empty: {
      title: '제공업체 없음',
      description: '시작하려면 LLM 제공업체를 추가하세요.'
    },
    form: {
      createTitle: '공급자 추가',
      editTitle: '제공자 편집',
      providerType: '제공자 유형',
      instanceName: '인스턴스 이름',
      instanceNameHint: '이 공급자 구성의 고유 이름입니다.',
      baseUrl: '기본 URL',
      defaultModel: '기본 모델',
      defaultContextWindow: '기본 컨텍스트 창',
      modelContextWindows: '모델별 컨텍스트 재정의',
      modelContextWindowsHint: 'JSON 객체(예: gpt-5.2: 200000',
      modelContextWindowsInvalid: '모델별 컨텍스트 재정의는 유효한 JSON이어야 합니다.',
      authType: '인증 유형',
      apiKey: 'API 키',
      apiKeyPlaceholder: '현재 키를 유지하려면 비워 두세요.',
      authFile: '인증 파일',
      oauthBrowserTitle: '브라우저 로그인(인증 코드)',
      oauthRedirectUri: '리디렉션 URI',
      oauthStartBrowser: '브라우저 로그인 시작',
      oauthAuthorizationUrl: '승인 URL',
      oauthState: '상태',
      oauthCallbackUrl: '콜백 URL 붙여넣기',
      oauthCallbackHint: '코드와 상태가 포함된 전체 콜백 URL을 붙여넣습니다.',
      oauthCompleteBrowser: '브라우저 로그인 완료',
      concurrency: '최대 동시성',
      create: '만들기',
      save: '저장',
      cancel: '취소',
      modelManualHint: '모델 ID를 수동으로 입력하세요. 이 공급자 유형에는 모델 목록을 사용할 수 없습니다.',
      modelsLockedHint: '모델 선택을 잠금 해제하려면 API 키로 공급자를 저장하세요.',
      savedContinue: '제공업체가 저장되었습니다. 이제 모델을 선택할 수 있습니다.',
      saveAndContinue: '저장하고 계속하기'
    },
    testSuccess: '공급자 "{id}"에 연결할 수 있습니다.',
    testFailed: '공급자 "{id}" 테스트에 실패했습니다.',
    createSuccess: '공급자 "{id}"이(가) 생성되었습니다.',
    updateSuccess: '공급자 "{id}"이(가) 업데이트되었습니다.',
    deleteSuccess: '\'{id}\' 제공업체가 삭제되었습니다.',
    defaultSuccess: '기본 공급자는 "{id}"로 설정됩니다.',
    errors: {
      loadFailed: '공급자를 로드하지 못했습니다.'
    }
  } as const;
