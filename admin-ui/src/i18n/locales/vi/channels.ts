export const channels = {
    title: 'Kênh',
    empty: 'Không có kênh nào được định cấu hình. Thêm một để bắt đầu.',
    createSuccess: 'Kênh "{name}" được tạo thành công.',
    updateSuccess: 'Kênh "{name}" được cập nhật thành công.',
    deleteSuccess: 'Kênh "{name}" đã bị xóa.',
    columns: {
      name: 'Tên',
      type: 'loại',
      identity: 'Danh tính',
      endpoint: 'Điểm cuối',
      enabled: 'Đã bật',
      connected: 'Đã kết nối',
      lastEvent: 'Sự kiện cuối cùng',
      actions: 'hành động'
    },
    state: {
      connected: 'Đã kết nối',
      disconnected: 'Đã ngắt kết nối'
    },
    actions: {
      refresh: 'Làm mới',
      add: 'Thêm kênh',
      cancel: 'Hủy bỏ',
      create: 'Tạo',
      save: 'Lưu',
      confirmDelete: 'Xóa kênh "{name}"? Điều này không thể hoàn tác được.'
    },
    form: {
      createTitle: 'Thêm kênh',
      editTitle: 'Chỉnh sửa "{name}"',
      name: 'Tên kênh',
      type: 'Loại kênh',
      agent: 'đại lý',
      minResponseInterval: 'Khoảng thời gian phản hồi tối thiểu (giây)',
      allowInterjection: 'Cho phép chèn',
      irc: {
        host: 'Máy chủ IRC',
        port: 'Cảng',
        serverPassword: 'Mật khẩu máy chủ',
        nick: 'Biệt danh',
        username: 'Tên người dùng',
        realname: 'Tên thật',
        channels: 'Kênh',
        channelsHint: 'Một trên mỗi dòng. Định dạng: #channel [phím]',
        agent: 'đại lý',
        respondToChannel: 'Trả lời tin nhắn kênh',
        respondToDm: 'Trả lời tin nhắn trực tiếp',
        respondToNotices: 'Trả lời thông báo',
        allowedDmUsers: 'Người dùng DM được phép',
        reconnect: 'Tự động kết nối lại'
      },
      telegram: {
        botToken: 'Mã thông báo bot',
        tokenHint: 'Để trống để giữ mã thông báo hiện có'
      },
      vk: {
        accessToken: 'Mã thông báo truy cập cộng đồng',
        groupId: 'Mã nhóm'
      },
      bluesky: {
        handle: 'xử lý',
        appPassword: 'Mật khẩu ứng dụng',
        pds: 'URL PDS'
      },
      mastodon: {
        handle: 'xử lý',
        instance: 'URL phiên bản'
      },
      email: {
        apiKey: 'Khóa API AgentMail',
        apiKeyHint: 'Để trống để giữ khóa hiện tại',
        inboxId: 'ID hộp thư đến',
        pollingWait: 'Khoảng thời gian thăm dò (giây)'
      },
      twitter: {
        tier: 'Cấp API',
        clientId: 'ID ứng dụng khách OAuth',
        clientSecret: 'Bí mật ứng dụng khách OAuth',
        accessToken: 'Mã thông báo truy cập',
        tokenHint: 'Để trống để giữ mã thông báo hiện có',
        refreshToken: 'Làm mới mã thông báo',
        authorize: 'Ủy quyền với Twitter',
        oauthStarted: 'Cửa sổ ủy quyền đã mở. Hoàn thành quy trình trong trình duyệt của bạn.'
      },
      discord: {
        botToken: 'Mã thông báo bot',
        tokenHint: 'Để trống để giữ mã thông báo hiện có',
        applicationId: 'ID ứng dụng (đối với lệnh gạch chéo)',
        monitoredChannels: 'Các kênh được giám sát',
        monitoredChannelsHint: 'Một ID kênh trên mỗi dòng. Bot phải ở trong máy chủ.',
        respondToDm: 'Trả lời DM',
        respondToMentions: 'Trả lời đề cập',
        dmWhitelistEnabled: 'Giới hạn DM cho người dùng được phép',
        allowedDmUsers: 'Người dùng DM được phép',
        allowedDmUsersHint: 'Tên người dùng Discord (mỗi dòng một tên). Chỉ những người dùng này mới có thể gửi DM cho bot.'
      },
      slack: {
        botToken: 'Mã thông báo Bot (xoxb-)',
        tokenHint: 'Để trống để giữ mã thông báo hiện có',
        appToken: 'Mã thông báo ứng dụng (xapp-)',
        appTokenHint: 'Đối với Chế độ ổ cắm (Giai đoạn 2). Tùy chọn cho Giai đoạn 1.',
        monitoredChannels: 'Kênh được giám sát (ghi đè)',
        monitoredChannelsHint: 'Bot tự động giám sát tất cả các kênh mà nó là thành viên. Chỉ thêm ID ở đây để hạn chế phạm vi.',
        respondToMentions: "Trả lời {'@'}đề cập",
        respondToAll: 'Trả lời tất cả tin nhắn (bỏ qua bộ lọc đề cập)',
        threadedReplies: 'Chủ đề trả lời trong các kênh (mỗi tin nhắn bắt đầu một chủ đề)'
      }
    }
  } as const;
