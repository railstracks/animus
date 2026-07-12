export const interfaces = {
    title: 'Quản lý giao diện',
    actions: {
      refresh: 'Làm mới',
      add: 'Thêm giao diện',
      edit: 'Chỉnh sửa',
      enable: 'Bật',
      disable: 'Tắt',
      delete: 'Xóa',
      confirmDelete: 'Xóa giao diện "{name}"?'
    },
    columns: {
      name: 'Tên',
      type: 'Loại',
      module: 'ID mô-đun',
      enabled: 'Đã bật',
      connected: 'Đã kết nối',
      lastEvent: 'Sự kiện gần nhất',
      actions: 'Hành động'
    },
    state: {
      enabled: 'có',
      disabled: 'không',
      connected: 'đã kết nối',
      disconnected: 'đã ngắt kết nối'
    },
    empty: 'Chưa có giao diện nào được cấu hình.',
    form: {
      createTitle: 'Tạo giao diện',
      editTitle: 'Chỉnh sửa giao diện: {name}',
      name: 'Tên phiên bản',
      type: 'Loại giao diện',
      moduleId: 'ID mô-đun',
      enabled: 'Đã bật',
      configJson: 'JSON cấu hình',
      create: 'Tạo',
      save: 'Lưu',
      cancel: 'Hủy',
      irc: {
        host: 'Máy chủ',
        port: 'Cổng',
        nick: 'Biệt danh',
        serverPassword: 'Mật khẩu máy chủ',
        username: 'Tên người dùng',
        realname: 'Tên thật',
        dmOnly: 'Chế độ chỉ DM',
        respondToChannel: 'Phản hồi hoạt động kênh',
        respondToDm: 'Phản hồi tin nhắn trực tiếp',
        respondToNotices: 'Phản hồi thông báo',
        allowedDmUsers: 'Người dùng DM được phép',
        allowedDmUsersHint: 'Danh sách cho phép tùy chọn; phân tách bằng dấu phẩy hoặc dòng mới.',
        agentId: 'ID tác tử',
        reconnectEnabled: 'Bật kết nối lại',
        reconnectInitialDelay: 'Độ trễ kết nối lại ban đầu (ms)',
        reconnectMaxDelay: 'Độ trễ kết nối lại tối đa (ms)'
      }
    },
    createSuccess: 'Đã tạo giao diện "{name}".',
    updateSuccess: 'Đã cập nhật giao diện "{name}".',
    deleteSuccess: 'Đã xóa giao diện "{name}".'
  } as const;
