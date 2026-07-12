export const memory = {
    title: 'Trình duyệt bộ nhớ',
    actions: {
      refresh: 'Làm mới',
      triggerConsolidation: 'Chạy hợp nhất'
    },
    common: {
      notAvailable: 'không có'
    },
    layers: {
      title: 'Lớp',
      empty: 'Không tìm thấy lớp bộ nhớ nào.',
      horizon: 'Đường chân trời',
      consolidationInterval: 'Khoảng thời gian hợp nhất',
      retentionPolicy: 'Chính sách lưu giữ',
      perspectiveCurrent: 'Quan điểm hiện tại',
      perspectivePast: 'Quan điểm quá khứ',
      perspectiveFuture: 'Viễn cảnh tương lai',
      viewObservations: 'Xem quan sát'
    },
    consolidation: {
      title: 'Hợp nhất',
      activeJob: 'Công việc đang hoạt động',
      lastRun: 'Lần chạy cuối cùng',
      lastJob: 'Công việc cuối cùng',
      promoted: 'Được thăng chức',
      demoted: 'Bị giáng chức',
      archived: 'Đã lưu trữ',
      state: {
        running: 'Đang chạy',
        idle: 'Nhàn rỗi'
      }
    },
    list: {
      title: 'Quan sát',
      activeLayer: 'Lớp: {layer}',
      sortBy: 'Sắp xếp',
      orderBy: 'Đặt hàng',
      tagFilter: 'Lọc theo thẻ',
      pageSize: 'Kích thước trang',
      compactMode: 'Nhỏ gọn',
      openDetail: 'Chi tiết',
      emptyLayer: 'Chưa có quan sát nào trong lớp này.',
      emptyFilter: 'Không có quan sát nào khớp với bộ lọc thẻ hiện tại.',
      sort: {
        weight: 'cân nặng',
        age: 'Tuổi',
        tags: 'Thẻ'
      },
      order: {
        desc: 'Giảm dần',
        asc: 'Tăng dần'
      },
      columns: {
        content: 'Nội dung',
        tags: 'Thẻ',
        weight: 'cân nặng',
        age: 'Tuổi',
        decay: 'phân hủy',
        actions: 'hành động'
      }
    },
    detail: {
      title: 'Chi tiết quan sát',
      empty: 'Chọn một quan sát để kiểm tra và chỉnh sửa.',
      id: 'ID',
      content: 'Nội dung',
      layer: 'Lớp',

      timestamp: 'Dấu thời gian',
      demotionReason: 'Lý do giáng chức',
      demotionTimestamp: 'Dấu thời gian giáng chức',
      editWeight: 'cân nặng',
      editTags: 'Thẻ',
      save: 'Lưu',
      reset: 'Đặt lại',
      archive: 'Lưu trữ',
      saveSuccess: 'Đã cập nhật quan sát.',
      archiveSuccess: 'Đã lưu trữ quan sát.'
    }
  } as const;
