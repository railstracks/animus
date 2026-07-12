export const providers = {
    title: '提供者配置',
    actions: {
      refresh: '重新整理',
      add: '新增提供者',
      test: '測試',
      edit: '編輯',
      delete: '刪除',
      setDefault: '設定預設值',
      confirmDelete: '刪除提供者“{id}”？此操作無法撤銷。'
    },
    columns: {
      status: '地位',
      provider: '姓名',
      type: '類型',
      baseUrl: '基本網址',
      defaultModel: '模型',
      default: '預設',
      actions: '行動'
    },
    empty: {
      title: '無提供者',
      description: '新增 LLM 提供者以開始使用。'
    },
    form: {
      createTitle: '新增提供者',
      editTitle: '編輯提供者',
      providerType: '提供者類型',
      instanceName: '實例名稱',
      instanceNameHint: '此提供者配置的唯一名稱。',
      baseUrl: '基本網址',
      defaultModel: '預設型號',
      defaultContextWindow: '預設上下文視窗',
      modelContextWindows: '每個模型的上下文覆蓋',
      modelContextWindowsHint: 'JSON 對象，例如gpt-5.2：200000',
      modelContextWindowsInvalid: '每個模型上下文覆蓋必須是有效的 JSON。',
      authType: '驗證類型',
      apiKey: 'API金鑰',
      apiKeyPlaceholder: '留空以保留當前密鑰',
      authFile: '驗證文件',
      oauthBrowserTitle: '瀏覽器登入（授權碼）',
      oauthRedirectUri: '重定向URI',
      oauthStartBrowser: '開始瀏覽器登入',
      oauthAuthorizationUrl: '授權網址',
      oauthState: '狀態',
      oauthCallbackUrl: '貼上回呼 URL',
      oauthCallbackHint: '貼上包含程式碼和狀態的完整回呼 URL。',
      oauthCompleteBrowser: '完成瀏覽器登入',
      concurrency: '最大並發數',
      create: '創造',
      save: '節省',
      cancel: '取消',
      modelManualHint: '手動輸入型號 ID。型號清單不適用於此提供者類型。',
      modelsLockedHint: '使用 API 金鑰保存提供者以解鎖模型選擇。',
      savedContinue: '提供者已儲存。您現在可以選擇一個模型。',
      saveAndContinue: '保存並繼續'
    },
    testSuccess: '提供者「{id}」可以存取。',
    testFailed: '提供者“{id}”測試失敗',
    createSuccess: '提供者「{id}」已建立。',
    updateSuccess: '提供者「{id}」已更新。',
    deleteSuccess: '提供者「{id}」已刪除。',
    defaultSuccess: '預設提供程序設定為“{id}”。',
    errors: {
      loadFailed: '無法載入提供者'
    }
  } as const;
