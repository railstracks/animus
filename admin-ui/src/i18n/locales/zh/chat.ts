export const chat = {
    sessionLabel: '会议',
    newSession: '新会议',
    refresh: '刷新',
    stopGenerating: '停止生成',
    emptyTitle: '开始对话',
    emptyDescription: '向 Animus 询问有关内存、配置或实时会话的信息以开始滚动。',
    streamState: {
      streaming: '流媒体...',
      stopped: '停止了'
    },
    jumpToLatest: '跳转到最新内容',
    composerPlaceholder: '给 Animus 发送消息...',
    adminTokenLabel: '管理员令牌（可选）',
    send: '发送',
    contextTitle: '背景',
    context: {
      session: '会议',
      layers: '层数',
      tools: '工具',
      budget: '预算',
      fallbackNote: '上下文快照随着会话的变化而更新。'
    },
    sidebarTabs: {
      context: '背景',
      sessions: '会议',
      messages: '消息',
      noSessions: '还没有可用的会话。',
      searchSessions: 'Search sessions...',
      newSessionHint: '撰写消息以启动新线程。'
    },
    thinking: {
      label: "思考"
    },
    toolResult: {
      label: "工具结果"
    },
    toolCall: {
      label: "工具调用"
    },
    reasoning: {
      label: '推理模式',
      enabled: '开',
      disabled: '关闭',
      effort: '努力',
      instructionLabel: '系统使用说明',
      instructionPlaceholder: '自定义推理指令（如果为空则使用默认值）',
      notSupported: '此模型无法进行推理。'
    },
    provider: {
      label: '提供者',
      select: '提供者',
      model: '型号'
    },
    agent: {
      label: '代理（新会话）'
    },
    status: {
      closed: '关闭',
      connecting: '正在连接',
      open: '已连接'
    },
    errors: {
      websocket: 'WebSocket 错误',
      websocketNotConnected: 'WebSocket 尚未连接。请再试一次。',
      agentNotReady: '代理选择仍在加载中。请稍等片刻，然后重试。',
      unknownWebsocket: '未知的 websocket 错误',
      sessionLoadFailed: '无法加载会话 {sessionId}：{message}'
    }
  } as const;
