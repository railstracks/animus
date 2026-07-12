export const providers = {
    title: 'プロバイダーの構成',
    actions: {
      refresh: 'リフレッシュ',
      add: 'プロバイダーの追加',
      test: 'テスト',
      edit: '編集',
      delete: '削除',
      setDefault: 'デフォルトの設定',
      confirmDelete: 'プロバイダー「{id}」を削除しますか?これを元に戻すことはできません。'
    },
    columns: {
      status: 'ステータス',
      provider: '名前',
      type: 'タイプ',
      baseUrl: 'ベース URL',
      defaultModel: 'モデル',
      default: 'デフォルト',
      actions: 'アクション'
    },
    empty: {
      title: 'プロバイダーがありません',
      description: 'まず、LLM プロバイダーを追加します。'
    },
    form: {
      createTitle: 'プロバイダーの追加',
      editTitle: 'プロバイダーの編集',
      providerType: 'プロバイダーの種類',
      instanceName: 'インスタンス名',
      instanceNameHint: 'このプロバイダー構成の一意の名前。',
      baseUrl: 'ベース URL',
      defaultModel: 'デフォルトのモデル',
      defaultContextWindow: 'デフォルトのコンテキストウィンドウ',
      modelContextWindows: 'モデルごとのコンテキストのオーバーライド',
      modelContextWindowsHint: 'JSON オブジェクト、例: gpt-5.2: 200000',
      modelContextWindowsInvalid: 'モデルごとのコンテキスト オーバーライドは有効な JSON である必要があります。',
      authType: '認証タイプ',
      apiKey: 'APIキー',
      apiKeyPlaceholder: '現在のキーを保持するには空のままにしておきます',
      authFile: '認証ファイル',
      oauthBrowserTitle: 'ブラウザログイン（認証コード）',
      oauthRedirectUri: 'リダイレクトURI',
      oauthStartBrowser: 'ブラウザログインを開始する',
      oauthAuthorizationUrl: '認可URL',
      oauthState: '州',
      oauthCallbackUrl: 'コールバック URL を貼り付け',
      oauthCallbackHint: 'コードと状態を含む完全なコールバック URL を貼り付けます。',
      oauthCompleteBrowser: 'ブラウザログインを完了する',
      concurrency: '最大同時実行数',
      create: '作成',
      save: '保存',
      cancel: 'キャンセル',
      modelManualHint: 'モデルIDを手動で入力します。このプロバイダー タイプではモデル リストを利用できません。',
      modelsLockedHint: 'API キーを使用してプロバイダーを保存し、モデル選択のロックを解除します。',
      savedContinue: 'プロバイダーが保存されました。モデルを選択できるようになりました。',
      saveAndContinue: '保存して続行'
    },
    testSuccess: 'プロバイダー「{id}」に到達可能です。',
    testFailed: 'プロバイダー「{id}」テストが失敗しました',
    createSuccess: 'プロバイダ「{id}」が作成されました。',
    updateSuccess: 'プロバイダ「{id}」が更新されました。',
    deleteSuccess: 'プロバイダ「{id}」が削除されました。',
    defaultSuccess: 'デフォルトのプロバイダーは「{id}」に設定されています。',
    errors: {
      loadFailed: 'プロバイダーのロードに失敗しました'
    }
  } as const;
