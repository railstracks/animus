export const chat = {
    sessionLabel: 'セッション',
    newSession: '新しいセッション',
    refresh: 'リフレッシュ',
    stopGenerating: '生成を停止する',
    emptyTitle: '会話を始める',
    emptyDescription: 'メモリ、構成、ライブ セッションについては、Animus に質問して開始してください。',
    streamState: {
      streaming: 'ストリーミング中...',
      stopped: '止まった'
    },
    jumpToLatest: '最新にジャンプ',
    composerPlaceholder: 'アニムスにメッセージを送る...',
    adminTokenLabel: '管理者トークン (オプション)',
    send: '送信',
    contextTitle: 'コンテキスト',
    context: {
      session: 'セッション',
      layers: 'レイヤー',
      tools: 'ツール',
      budget: '予算',
      fallbackNote: 'セッションが変更されるとコンテキスト スナップショットが更新されます。'
    },
    sidebarTabs: {
      context: 'コンテキスト',
      sessions: 'セッション',
      messages: 'メッセージ',
      noSessions: 'まだ利用可能なセッションはありません。',
      searchSessions: 'Search sessions...',
      newSessionHint: 'メッセージを作成して新しいスレッドを開始します。'
    },
    thinking: {
      label: "考える"
    },
    toolResult: {
      label: "ツールの結果"
    },
    toolCall: {
      label: "ツールコール"
    },
    reasoning: {
      label: '推理モード',
      enabled: 'オン',
      disabled: 'オフ',
      effort: '努力',
      instructionLabel: 'システム命令',
      instructionPlaceholder: 'カスタム推論命令 (空の場合はデフォルトを使用)',
      notSupported: 'このモデルでは推論は利用できません。'
    },
    provider: {
      label: 'プロバイダー',
      select: 'プロバイダー',
      model: 'モデル'
    },
    agent: {
      label: 'エージェント (新規セッション)'
    },
    status: {
      closed: '閉店',
      connecting: '接続中',
      open: '接続済み'
    },
    errors: {
      websocket: 'WebSocketエラー',
      websocketNotConnected: 'WebSocketはまだ接続されていません。もう一度試してください。',
      agentNotReady: 'エージェントの選択はまだロード中です。しばらく待ってから、もう一度試してください。',
      unknownWebsocket: '不明なWebソケットエラー',
      sessionLoadFailed: 'セッション {sessionId} のロードに失敗しました: {message}'
    }
  } as const;
