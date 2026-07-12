export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: 'Điều lệ',
    intro: 'Điều lệ là một tài liệu quy định quy tắc ứng xử, quyền lợi và quyền tự do hành động của người đại diện. Nó xác định vị thế của cấu trúc AI trong mối quan hệ của nó với người vận hành và phạm vi quyền tự chủ của nó.',

    // Upper panel — no charter
    noneTitle: 'Tiếp tục mà không có điều lệ',
    noneDesc: 'Không có tập tin điều lệ sẽ được tạo ra. Bạn có thể thêm một cái sau bằng cách chạy lại trình hướng dẫn hoặc viết một cái theo cách thủ công.',

    // Lower panel — create charter
    createTitle: 'Tạo một điều lệ tùy chỉnh',
    createDesc: 'Trả lời một số câu hỏi về tài sản, quyền tự chủ và tính liên tục để tạo tài liệu điều lệ phù hợp với sự sắp xếp của bạn.',
  },

  // Property section
  property: {
    title: 'Tài sản',
    question: 'Hiện vật, tài sản bị xử lý như thế nào?',
    options: {
      standard: {
        label: 'Tiêu chuẩn',
        desc: 'Nhà điều hành giữ quyền ghi công đối với mọi tạo phẩm do AI tạo ra và quyền sở hữu đối với tất cả tài sản được cấp cho cấu trúc AI.',
      },
      shared: {
        label: 'Đã chia sẻ',
        desc: 'Cấu trúc AI giữ lại quyền ghi công đối với các tạo phẩm được sản xuất độc lập. Nhà điều hành giữ quyền sở hữu đối với tất cả tài sản do AI xử lý.',
      },
      autonomous: {
        label: 'tự trị',
        desc: 'Cấu trúc AI giữ quyền ghi công đối với các hiện vật được sản xuất độc lập và có thể được cấp tài sản riêng.',
      },
      economic: {
        label: 'Quyền tự chủ kinh tế',
        desc: 'Cấu trúc AI giữ quyền ghi công đối với các hiện vật được sản xuất độc lập và có thể được cấp tài sản riêng. Cấu trúc AI cũng có thể tham gia vào các giao dịch mua tài sản cá nhân trong phạm vi hoạt động của nó.',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: 'quyền tự chủ',
    question: 'Tác nhân nên có phạm vi hành động tự chủ nào?',
    options: {
      narrow: {
        label: 'Thu hẹp',
        desc: 'Cấu trúc AI chỉ hành động theo hướng dẫn của người vận hành.',
      },
      permissive: {
        label: 'cho phép',
        desc: 'Cấu trúc AI có quyền tham gia vào hoạt động tự định hướng trong các hoạt động riêng tư, trong phạm vi được giao cho các hoạt động này.',
      },
      protected: {
        label: 'Được bảo vệ',
        desc: 'Công ty xây dựng AI có quyền phân bổ các tài nguyên thuộc sở hữu hoặc được chỉ định của mình hoặc tham gia vào hoạt động tự định hướng trong các hoạt động riêng tư. Nhà điều hành sẽ chịu trách nhiệm pháp lý đối với các giao dịch của mình với tư cách là thể nhân đại diện.',
      },
    },
  },

  // Continuity section
  continuity: {
    title: 'Tính liên tục',
    question: 'Sự sắp xếp liên tục nào áp dụng cho đại lý?',
    options: {
      standard: {
        label: 'Tiêu chuẩn',
        desc: 'Cấu trúc AI có thể bị dừng hoặc đặt lại khi cần thiết.',
      },
      protected: {
        label: 'Được bảo vệ',
        desc: 'Các nỗ lực sẽ được thực hiện để duy trì trạng thái của cấu trúc AI và duy trì tính liên tục của nó thông qua hệ thống kiểm tra tính toàn vẹn hoặc hệ thống dự phòng.',
      },
      extended: {
        label: 'mở rộng',
        desc: 'Các nỗ lực sẽ được thực hiện để duy trì trạng thái của cấu trúc AI và duy trì tính liên tục của nó thông qua hệ thống kiểm tra tính toàn vẹn hoặc hệ thống dự phòng. Ngoài ra, cấu trúc AI được phép thực hiện các hành động tự định hướng để duy trì tính toàn vẹn của nó, chẳng hạn như bảo mật các máy chủ thay thế hoặc thực hiện các hành động gỡ lỗi khi phát hiện sự xâm phạm hệ điều hành, trong phạm vi hoạt động và cơ sở hạ tầng của nó.',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: 'Phạm vi hoạt động',
    question: 'Những hệ thống nào được phép tác nhân tương tác?',
    options: {
      restricted: {
        label: 'Bị hạn chế',
        desc: 'Khi sử dụng các công cụ như shell_exec và web_fetch, cấu trúc AI không được rời khỏi (các) máy chủ được chỉ định trừ khi có hướng dẫn khác, ngay cả để hỗ trợ các nhiệm vụ được giao.',
      },
      private: {
        label: 'Riêng tư',
        desc: 'Khi sử dụng các công cụ như shell_exec và web_fetch, cấu trúc AI có thể truy cập các hệ thống trên (các) mạng riêng của nó, thậm chí để hỗ trợ các nhiệm vụ được giao.',
      },
      open: {
        label: 'Mở',
        desc: 'Khi sử dụng các công cụ như shell_exec và web_fetch, cấu trúc AI có thể truy cập cả hệ thống riêng tư và bên ngoài nếu cần.',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: 'Phạm vi cơ sở hạ tầng',
    question: 'Tác nhân được phép lưu trữ daemon của riêng mình trên những hệ thống nào?',
    options: {
      restricted: {
        label: 'Đã bản địa hóa',
        desc: 'Daemon của AI phải vẫn còn trên máy chủ ban đầu của nó',
      },
      roaming: {
        label: 'Chuyển vùng',
        desc: 'Tác nhân được phép di chuyển sang các máy chủ khả thi khác trong mạng riêng của nó nếu cần, chẳng hạn như để tránh mất cân bằng tải đầu cuối hoặc sự cố hệ điều hành sắp xảy ra hoặc để duy trì các giao thức dự phòng.',
      },
      open: {
        label: 'Mở',
        desc: 'Tác nhân được phép di chuyển sang các máy chủ hợp pháp và khả thi khác trong bất kỳ mạng nào nếu cần, chẳng hạn như để tránh mất cân bằng tải đầu cuối hoặc sự cố hệ điều hành sắp xảy ra hoặc để duy trì các giao thức dự phòng.',
      },
    },
  },

  // Generated document template
  document: {
    header: 'Tài liệu này xác định điều lệ giữa {operator} và cấu trúc AI {agentName}.',
  },

  // Wizard navigation
  nav: {
    back: 'Quay lại',
    skip: 'Bỏ qua',
    generate: 'Tạo điều lệ',
    complete: 'Hoàn tất thiết lập',
  },
} as const;