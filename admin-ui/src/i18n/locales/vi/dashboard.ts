export const dashboard = {
  // First-run banner
  banner: {
    welcome: 'Chào mừng đến với Animus.',
    welcomeSub: 'Thiết lập đại lý đầu tiên của bạn để bắt đầu.',
    beginSetup: 'Bắt đầu thiết lập →',
  },
  // Status ribbon
  ribbon: {
    uptime: 'Thời gian hoạt động',
    agents: 'Đại lý',
    sessions: 'Phiên',
    providers: 'Nhà cung cấp',
    errors: 'lỗi',
  },
  // Recent Sessions card
  recentSessions: {
    title: 'Phiên gần đây',
    viewAll: 'Xem tất cả',
    empty: 'Chưa có phiên nào.',
    startChat: 'Bắt đầu trò chuyện →',
    messages: 'tin nhắn',
  },
  // Memory card
  memory: {
    title: 'Bộ nhớ',
    inspect: 'Kiểm tra',
    empty: 'Không có lớp bộ nhớ nào được cấu hình.',
    totalObservations: 'tổng số quan sát',
  },
  // Provider card
  providers: {
    title: 'Nhà cung cấp',
    manage: 'Quản lý',
    empty: 'Không có nhà cung cấp nào được định cấu hình.',
    runWizard: 'Chạy trình hướng dẫn thiết lập →',
  },
  // Quick Actions card
  quickActions: {
    title: 'Thao tác nhanh',
    newChat: 'Trò chuyện mới',
    addAgent: 'Thêm đại lý',
    provider: 'nhà cung cấp',
    logs: 'Nhật ký',
    scheduler: 'Người lập lịch trình',
    memory: 'Bộ nhớ',
  },
  // Daemon Info card
  daemonInfo: {
    title: 'Thông tin daemon',
    status: 'Trạng thái',
    uptime: 'Thời gian hoạt động',
    agents: 'Đại lý',
    activeSessions: 'Phiên hoạt động',
    providers: 'Nhà cung cấp',
  },
  // Memory Layers card
  memoryLayers: {
    title: 'Lớp bộ nhớ',
    empty: 'Không có lớp bộ nhớ nào được cấu hình.',
  },
  // State labels
  state: {
    loading: 'đang tải',
    unknown: 'không rõ',
  },
  // Error messages
  errors: {
    sessions: 'Không tải được phiên',
    providers: 'Không tải được nhà cung cấp',
    memory: 'API bộ nhớ không khả dụng',
  },
} as const;