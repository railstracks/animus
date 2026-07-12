export const ontology = {
    title: 'Trình khám phá bản thể học',
    subtitle: 'Duyệt qua các thực thể ngữ nghĩa và các thuộc tính dựa trên quan sát của chúng.',
    actions: {
      refresh: 'Làm mới'
    },
    sections: {
      agent: 'đại lý',
      categories: 'Danh mục',
      entities: 'Thực thể',
      details: 'Chi tiết thực thể',
      properties: 'Thuộc tính'
    },
    states: {
      new: 'mới',
      current: 'hiện tại',
      deprecated: 'không được dùng nữa'
    },
    empty: {
      entities: 'Không tìm thấy thực thể nào cho danh mục này.',
      details: 'Chọn một thực thể để kiểm tra thuộc tính.'
    }
  } as const;
