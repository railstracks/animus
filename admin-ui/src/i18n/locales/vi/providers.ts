export const providers = {
    title: 'Cấu hình nhà cung cấp',
    actions: {
      refresh: 'Làm mới',
      add: 'Thêm nhà cung cấp',
      test: 'kiểm tra',
      edit: 'Chỉnh sửa',
      delete: 'Xóa',
      setDefault: 'Đặt mặc định',
      confirmDelete: 'Xóa nhà cung cấp "{id}"? Điều này không thể hoàn tác được.'
    },
    columns: {
      status: 'Trạng thái',
      provider: 'Tên',
      type: 'loại',
      baseUrl: 'URL cơ sở',
      defaultModel: 'người mẫu',
      default: 'Mặc định',
      actions: 'hành động'
    },
    empty: {
      title: 'Không có nhà cung cấp',
      description: 'Thêm nhà cung cấp LLM để bắt đầu.'
    },
    form: {
      createTitle: 'Thêm nhà cung cấp',
      editTitle: 'Chỉnh sửa nhà cung cấp',
      providerType: 'Loại nhà cung cấp',
      instanceName: 'Tên phiên bản',
      instanceNameHint: 'Tên duy nhất cho cấu hình nhà cung cấp này.',
      baseUrl: 'URL cơ sở',
      defaultModel: 'Mẫu mặc định',
      defaultContextWindow: 'Cửa sổ ngữ cảnh mặc định',
      modelContextWindows: 'Ghi đè bối cảnh trên mỗi mô hình',
      modelContextWindowsHint: 'Đối tượng JSON, ví dụ: gpt-5.2: 200000',
      modelContextWindowsInvalid: 'Phần ghi đè ngữ cảnh trên mỗi mô hình phải là JSON hợp lệ.',
      authType: 'Loại xác thực',
      apiKey: 'Khóa API',
      apiKeyPlaceholder: 'Để trống để giữ khóa hiện tại',
      authFile: 'Tệp xác thực',
      oauthBrowserTitle: 'Đăng nhập trình duyệt (Mã ủy quyền)',
      oauthRedirectUri: 'URI chuyển hướng',
      oauthStartBrowser: 'Bắt đầu đăng nhập trình duyệt',
      oauthAuthorizationUrl: 'URL ủy quyền',
      oauthState: 'tiểu bang',
      oauthCallbackUrl: 'Dán URL gọi lại',
      oauthCallbackHint: 'Dán URL gọi lại đầy đủ chứa mã và trạng thái.',
      oauthCompleteBrowser: 'Hoàn tất đăng nhập trình duyệt',
      concurrency: 'Đồng thời tối đa',
      create: 'Tạo',
      save: 'Lưu',
      cancel: 'Hủy bỏ',
      modelManualHint: 'Nhập ID mẫu theo cách thủ công. Danh sách mô hình không có sẵn cho loại nhà cung cấp này.',
      modelsLockedHint: 'Lưu nhà cung cấp bằng khóa API để mở khóa lựa chọn mô hình.',
      savedContinue: 'Đã lưu nhà cung cấp. Bây giờ bạn có thể chọn một mô hình.',
      saveAndContinue: 'Lưu & Tiếp tục'
    },
    testSuccess: 'Nhà cung cấp "{id}" có thể truy cập được.',
    testFailed: 'Thử nghiệm của nhà cung cấp "{id}" không thành công',
    createSuccess: 'Đã tạo nhà cung cấp "{id}".',
    updateSuccess: 'Đã cập nhật nhà cung cấp "{id}".',
    deleteSuccess: 'Nhà cung cấp "{id}" đã bị xóa.',
    defaultSuccess: 'Nhà cung cấp mặc định được đặt thành "{id}".',
    errors: {
      loadFailed: 'Không tải được nhà cung cấp'
    }
  } as const;
