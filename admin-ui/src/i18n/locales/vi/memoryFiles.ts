export const memoryFiles = {
    title: 'Tập tin bộ nhớ',
    subtitle: 'Lưu trữ các tạo phẩm văn bản thô và tìm kiếm trên các miền bộ nhớ.',
    common: {
      yes: 'vâng',
      no: 'không'
    },
    actions: {
      refresh: 'Làm mới',
      open: 'Mở',
      delete: 'Xóa',
      process: 'Hàng đợi xử lý',
      import: 'Nhập khẩu',
      importBatch: 'Lô hàng nhập khẩu',
      saveMetadata: 'Lưu siêu dữ liệu',
      search: 'Tìm kiếm'
    },
    fields: {
      sourcePath: 'Đường dẫn nguồn',
      fileType: 'Loại tệp',
      contentMutable: 'Nội dung có thể thay đổi',
      agentId: 'ID đại lý',
      status: 'Trạng thái',
      agentFilter: 'Lọc theo đại lý',
      allAgents: 'Tất cả đại lý',
      superseded: 'Đã thay thế',
      content: 'Nội dung'
    },
    types: {
      all: 'Tất cả các loại',
      expanded_memory: 'Bộ nhớ mở rộng',
      session_log: 'Nhật ký phiên',
      daily_note: 'Ghi chú hàng ngày',
      bootstrap_file: 'Tệp khởi động',
      journal: 'tạp chí',
      external_doc: 'Tài liệu bên ngoài'
    },
    stats: {
      title: 'Thống kê'
    },
    list: {
      title: 'Danh sách tập tin',
      typeFilter: 'Loại bộ lọc',
      limit: 'giới hạn',
      offset: 'Bù đắp',
      columns: {
        id: 'ID',
        type: 'loại',
        sourcePath: 'Đường dẫn nguồn',
        contentMutable: 'Có thể thay đổi',
        agentId: 'ID đại lý',
      status: 'Trạng thái',
      agentFilter: 'Lọc theo đại lý',
      allAgents: 'Tất cả đại lý',
        superseded: 'Đã thay thế',
        importedAt: 'Đã nhập',
        actions: 'hành động'
      },
      status: {
        unprocessed: 'Chưa xử lý',
        processed: 'Đã xử lý'
      }
    },
    import: {
      singleTitle: 'Nhập tệp',
      selectFile: 'Chọn tập tin',
      selected: 'Đã chọn',
      batchTitle: 'Nhập hàng loạt',
      selectFiles: 'Chọn tập tin',
      filesSelected: 'tập tin đã chọn'
    },
    detail: {
      title: 'Chi tiết tệp',
      empty: 'Chọn một tệp từ danh sách để kiểm tra và chỉnh sửa siêu dữ liệu.',
      createdAt: 'Đã tạo',
      importedAt: 'Đã nhập',
      contentTitle: 'Nội dung',
      contentImmutableNotice: 'Chỉnh sửa nội dung bị vô hiệu hóa vì tệp này được đánh dấu là không thể thay đổi.'
    },
    search: {
      title: 'Tìm kiếm hợp nhất',
      query: 'Truy vấn tìm kiếm',
      limit: 'giới hạn',
      relevance: 'Mức độ liên quan',
      empty: 'Chưa có kết quả tìm kiếm nào.',
      domains: {
        observation: 'Quan sát',
        observations: 'Quan sát',
        ontology: 'Bản thể học',
        memory_file: 'Tập tin',
        sessions: 'Phiên'
      }
    },
    errors: {
      loadFiles: 'Không thể tải tập tin bộ nhớ.',
      loadDetail: 'Không thể tải chi tiết tệp.',
      importRequired: 'Chọn một tập tin để nhập khẩu.',
      importSingle: 'Không thể nhập tập tin bộ nhớ.',
      batchRequired: 'Chọn ít nhất một tệp để nhập hàng loạt.',
      importBatch: 'Không thể nhập tải trọng hàng loạt.',
      updateMetadata: 'Không cập nhật được siêu dữ liệu.',
      delete: 'Không thể xóa tập tin bộ nhớ.',
      search: 'Tìm kiếm bộ nhớ không thành công.'
    }
  } as const;
