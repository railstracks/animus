export const dashboard = {
  // First-run banner
  banner: {
    welcome: '歡迎來到阿尼姆斯。',
    welcomeSub: '設定您的第一個代理即可開始。',
    beginSetup: '開始設定 →',
  },
  // Status ribbon
  ribbon: {
    uptime: '正常運作時間',
    agents: '代理商',
    sessions: '會議',
    providers: '供應商',
    errors: '犯錯',
  },
  // Recent Sessions card
  recentSessions: {
    title: '最近的會議',
    viewAll: '看全部',
    empty: '還沒有會議。',
    startChat: '開始聊天 →',
    messages: '訊息',
  },
  // Memory card
  memory: {
    title: '記憶',
    inspect: '檢查',
    empty: '未配置記憶體層。',
    totalObservations: '總觀察值',
  },
  // Provider card
  providers: {
    title: '供應商',
    manage: '管理',
    empty: '沒有配置提供者。',
    runWizard: '運行安裝精靈 →',
  },
  // Quick Actions card
  quickActions: {
    title: '快速行動',
    newChat: '新聊天',
    addAgent: '新增代理',
    provider: '提供者',
    logs: '紀錄',
    scheduler: '調度程式',
    memory: '記憶',
  },
  // Daemon Info card
  daemonInfo: {
    title: '守護程式資訊',
    status: '地位',
    uptime: '正常運作時間',
    agents: '代理商',
    activeSessions: '活躍會話',
    providers: '供應商',
  },
  // Memory Layers card
  memoryLayers: {
    title: '記憶體層',
    empty: '未配置記憶體層。',
  },
  // State labels
  state: {
    loading: '載入中',
    unknown: '未知',
  },
  // Error messages
  errors: {
    sessions: '無法載入會話',
    providers: '無法載入提供者',
    memory: '記憶體API不可用',
  },
} as const;