export const activeMemory = {
    title: 'Bộ nhớ hoạt động',
    subtitle: 'Bối cảnh tác nhân được lắp ráp - những gì tác nhân nhìn thấy trong phần mở đầu của nó',
    empty: 'Chọn một tác nhân để xem bối cảnh được tập hợp của nó.',
    actions: {
      refresh: 'Làm mới'
    },
    labels: {
      agent: 'đại lý',
      session: 'Phiên',
      syntheticSession: 'Tổng hợp (phiên trống để thử nghiệm)',
      blocks: 'khối'
    },
    flags: {
      synthetic: 'Phiên tổng hợp',
      live: 'Phiên trực tiếp'
    },
    sections: {
      rendered: 'Đầu ra được kết xuất',
      blocks: 'Phân tích khối'
    }
  } as const;
