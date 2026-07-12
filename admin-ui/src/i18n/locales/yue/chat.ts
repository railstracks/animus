export const chat = {
    sessionLabel: '會議',
    newSession: '新會議',
    refresh: '重新整理',
    stopGenerating: '停止生成',
    emptyTitle: '開始對話',
    emptyDescription: '向 Animus 詢問有關記憶體、配置或即時會話的資訊以開始捲動。',
    streamState: {
      streaming: '串流媒體...',
      stopped: '停止了'
    },
    jumpToLatest: '跳到最新內容',
    composerPlaceholder: '給 Animus 發送訊息...',
    adminTokenLabel: '管理員令牌（可選）',
    send: '傳送',
    contextTitle: '情境',
    context: {
      session: '會議',
      layers: '層數',
      tools: '工具',
      budget: '預算',
      fallbackNote: '上下文快照隨著會話的變化而更新。'
    },
    sidebarTabs: {
      context: '情境',
      sessions: '會議',
      messages: '訊息',
      noSessions: '還沒有可用的會話。',
      searchSessions: 'Search sessions...',
      newSessionHint: '撰寫訊息以啟動新線程。'
    },
    thinking: {
      label: "思維"
    },
    toolResult: {
      label: "工具結果"
    },
    toolCall: {
      label: "工具調用"
    },
    reasoning: {
      label: '推理模式',
      enabled: '在',
      disabled: '離開',
      effort: '努力',
      instructionLabel: '系統使用說明',
      instructionPlaceholder: '自訂推理指令（如果為空則使用預設值）',
      notSupported: '此模型無法進行推理。'
    },
    provider: {
      label: '提供者',
      select: '提供者',
      model: '模型'
    },
    agent: {
      label: '代理（新會話）'
    },
    status: {
      closed: '關閉',
      connecting: '正在連接',
      open: '已連接'
    },
    errors: {
      websocket: 'WebSocket 錯誤',
      websocketNotConnected: 'WebSocket 尚未連線。請再試一次。',
      agentNotReady: '代理選擇仍在加載中。請稍等片刻，然後重試。',
      unknownWebsocket: '未知的 websocket 錯誤',
      sessionLoadFailed: '無法載入會話 {sessionId}：{message}'
    }
  } as const;
