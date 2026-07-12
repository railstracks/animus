export const dashboard = {
  // First-run banner
  banner: {
    welcome: '欢迎来到阿尼姆斯。',
    welcomeSub: '设置您的第一个代理即可开始。',
    beginSetup: '开始设置 →',
  },
  // Status ribbon
  ribbon: {
    uptime: '正常运行时间',
    agents: '代理商',
    sessions: '会议',
    providers: '供应商',
    errors: '犯错',
  },
  // Recent Sessions card
  recentSessions: {
    title: '最近的会议',
    viewAll: '查看全部',
    empty: '还没有会议。',
    startChat: '开始聊天 →',
    messages: '消息',
  },
  // Memory card
  memory: {
    title: '内存',
    inspect: '检查',
    empty: '未配置内存层。',
    totalObservations: '总观察值',
  },
  // Provider card
  providers: {
    title: '供应商',
    manage: '管理',
    empty: '没有配置提供者。',
    runWizard: '运行安装向导 →',
  },
  // Quick Actions card
  quickActions: {
    title: '快速行动',
    newChat: '新聊天',
    addAgent: '添加代理',
    provider: '提供者',
    logs: '日志',
    scheduler: '调度程序',
    memory: '内存',
  },
  // Daemon Info card
  daemonInfo: {
    title: '守护进程信息',
    status: '状态',
    uptime: '正常运行时间',
    agents: '代理商',
    activeSessions: '活跃会话',
    providers: '供应商',
  },
  // Memory Layers card
  memoryLayers: {
    title: '内存层',
    empty: '未配置内存层。',
  },
  // State labels
  state: {
    loading: '加载中',
    unknown: '未知',
  },
  // Error messages
  errors: {
    sessions: '无法加载会话',
    providers: '无法加载提供商',
    memory: '内存API不可用',
  },
} as const;