export const chat = {
    sessionLabel: 'Phiên',
    newSession: 'Phiên mới',
    refresh: 'Làm mới',
    stopGenerating: 'Dừng tạo',
    emptyTitle: 'Bắt đầu cuộc trò chuyện',
    emptyDescription: 'Hãy hỏi Animus về bộ nhớ, cấu hình hoặc phiên trực tiếp để bắt đầu.',
    streamState: {
      streaming: 'đang phát sóng...',
      stopped: 'dừng lại'
    },
    jumpToLatest: 'Chuyển tới phần mới nhất',
    composerPlaceholder: 'Gửi tin nhắn tới Animus...',
    adminTokenLabel: 'Mã thông báo quản trị (tùy chọn)',
    send: 'Gửi',
    contextTitle: 'Bối cảnh',
    context: {
      session: 'Phiên',
      layers: 'Lớp',
      tools: 'Công cụ',
      budget: 'Ngân sách',
      fallbackNote: 'Cập nhật ảnh chụp nhanh bối cảnh khi phiên thay đổi.'
    },
    sidebarTabs: {
      context: 'Bối cảnh',
      sessions: 'Phiên',
      messages: 'tin nhắn',
      noSessions: 'Chưa có phiên nào.',
      searchSessions: 'Search sessions...',
      newSessionHint: 'Soạn tin nhắn để bắt đầu một chủ đề mới.'
    },
    thinking: {
      label: "suy nghĩ"
    },
    toolResult: {
      label: "Kết quả công cụ"
    },
    toolCall: {
      label: "Cuộc gọi công cụ"
    },
    reasoning: {
      label: 'Chế độ suy luận',
      enabled: 'Bật',
      disabled: 'Tắt',
      effort: 'nỗ lực',
      instructionLabel: 'Hướng dẫn hệ thống',
      instructionPlaceholder: 'Hướng dẫn suy luận tùy chỉnh (sử dụng mặc định nếu trống)',
      notSupported: 'Lý luận không có sẵn cho mô hình này.'
    },
    provider: {
      label: 'nhà cung cấp',
      select: 'nhà cung cấp',
      model: 'người mẫu'
    },
    agent: {
      label: 'Đại lý (Phiên mới)'
    },
    status: {
      closed: 'Đã đóng',
      connecting: 'Đang kết nối',
      open: 'Đã kết nối'
    },
    errors: {
      websocket: 'Lỗi WebSocket',
      websocketNotConnected: 'WebSocket chưa được kết nối. Vui lòng thử lại.',
      agentNotReady: 'Lựa chọn đại lý vẫn đang tải. Vui lòng đợi một lát và thử lại.',
      unknownWebsocket: 'Lỗi websocket không xác định',
      sessionLoadFailed: 'Không tải được phiên {sessionId}: {message}'
    }
  } as const;
