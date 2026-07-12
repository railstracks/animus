export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'アニムスへようこそ。',
    welcomeSub: '最初のエージェントをセットアップして開始します。',
    beginSetup: 'セットアップを開始する →',
  },
  // Status ribbon
  ribbon: {
    uptime: '稼働時間',
    agents: 'エージェント',
    sessions: 'セッション',
    providers: 'プロバイダー',
    errors: 'エラー',
  },
  // Recent Sessions card
  recentSessions: {
    title: '最近のセッション',
    viewAll: 'すべて見る',
    empty: 'まだセッションはありません。',
    startChat: 'チャットを開始する →',
    messages: 'メッセージ',
  },
  // Memory card
  memory: {
    title: '記憶',
    inspect: '検査する',
    empty: 'メモリ層が設定されていません。',
    totalObservations: '合計観測値',
  },
  // Provider card
  providers: {
    title: 'プロバイダー',
    manage: '管理する',
    empty: 'プロバイダーが構成されていません。',
    runWizard: 'セットアップウィザードを実行 →',
  },
  // Quick Actions card
  quickActions: {
    title: 'クイックアクション',
    newChat: '新しいチャット',
    addAgent: 'エージェントの追加',
    provider: 'プロバイダー',
    logs: 'ログ',
    scheduler: 'スケジューラー',
    memory: '記憶',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'デーモン情報',
    status: 'ステータス',
    uptime: '稼働時間',
    agents: 'エージェント',
    activeSessions: 'アクティブなセッション',
    providers: 'プロバイダー',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'メモリ層',
    empty: 'メモリ層が設定されていません。',
  },
  // State labels
  state: {
    loading: '読み込み中',
    unknown: '不明',
  },
  // Error messages
  errors: {
    sessions: 'セッションのロードに失敗しました',
    providers: 'プロバイダーのロードに失敗しました',
    memory: 'メモリ API が使用できません',
  },
} as const;