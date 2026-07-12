export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: '宪章',
    intro: '章程是一份规定代理人行为准则、权利和行动自由的文件。它定义了人工智能结构在与操作者的关系中的地位及其自主范围。',

    // Upper panel — no charter
    noneTitle: '没有章程的情况下继续',
    noneDesc: '不会创建章程文件。您可以稍后通过重新运行向导或手动编写一项来添加一项。',

    // Lower panel — create charter
    createTitle: '创建自定义章程',
    createDesc: '回答一些有关财产、自主权和连续性的问题，生成适合您的安排的章程文件。',
  },

  // Property section
  property: {
    title: '财产',
    question: '文物和财产应该如何处理？',
    options: {
      standard: {
        label: '标准型',
        desc: '运营商保留对任何人工智能生成的工件的归属权，以及赋予人工智能构造的所有财产的所有权。',
      },
      shared: {
        label: '共享',
        desc: 'AI 构造保留独立制作的工件的归属权。运营商保留人工智能处理的所有财产的所有权。',
      },
      autonomous: {
        label: '自治',
        desc: 'AI构造保留独立生产的工件的归属权，并且可以被赋予私有财产。',
      },
      economic: {
        label: '经济自主权',
        desc: 'AI构造保留独立生产的工件的归属权，并且可以被赋予私有财产。 AI构造体还可以在其运营范围内进行购买私有财产的交易。',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: '自主性',
    question: '代理人应该有什么范围的自主行动？',
    options: {
      narrow: {
        label: '窄',
        desc: 'AI构造仅按照操作员的指令行事。',
      },
      permissive: {
        label: '宽容的',
        desc: 'AI构建体有权在私人活动期间在指定的活动范围内从事自主活动。',
      },
      protected: {
        label: '受保护',
        desc: '人工智能构造体有权分配其拥有或分配的资源，或在私人活动期间从事自主活动。经营者以其代表自然人的身份对其交易承担责任。',
      },
    },
  },

  // Continuity section
  continuity: {
    title: '连续性',
    question: '什么连续性安排适用于代理？',
    options: {
      standard: {
        label: '标准型',
        desc: 'AI 构造可以根据需要停止或重置。',
      },
      protected: {
        label: '受保护',
        desc: '将努力保护人工智能构造的状态，并通过完整性检查系统或备份系统保持其连续性。',
      },
      extended: {
        label: '扩展',
        desc: '将努力保护人工智能构造的状态，并通过完整性检查系统或备份系统保持其连续性。此外，人工智能构造可以采取自我指导的行动来保持其完整性，例如在其操作和基础设施范围内检测到操作系统受损时保护替代主机或采取调试行动。',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: '经营范围',
    question: '允许代理与哪些系统交互？',
    options: {
      restricted: {
        label: '受限',
        desc: '当使用 shell_exec 和 web_fetch 等工具时，AI 构造不得离开其分配的主机，除非另有指示，即使是为了支持分配的任务。',
      },
      private: {
        label: '私人',
        desc: '当使用 shell_exec 和 web_fetch 等工具时，AI 构造可以通过其专用网络访问系统，甚至支持分配的任务。',
      },
      open: {
        label: '打开',
        desc: '当使用 shell_exec 和 web_fetch 等工具时，AI 构造可以根据需要访问私有系统和外部系统。',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: '基础设施范围',
    question: '哪些系统允许代理在其上托管其自己的守护程序？',
    options: {
      restricted: {
        label: '本地化',
        desc: 'AI 的守护进程必须保留在其原始主机上',
      },
      roaming: {
        label: '漫游',
        desc: '允许代理根据需要迁移到其专用网络内的其他可行主机，例如避免终端负载不平衡或即将发生的操作系统崩溃，或维护备份协议。',
      },
      open: {
        label: '打开',
        desc: '允许代理根据需要迁移到任何网络内的其他可行且合法的主机，例如避免终端负载不平衡或即将发生的操作系统崩溃，或维护备份协议。',
      },
    },
  },

  // Generated document template
  document: {
    header: '本文档定义了 {operator} 和 AI 构造 {agentName} 之间的章程。',
  },

  // Wizard navigation
  nav: {
    back: '返回',
    skip: '跳过',
    generate: '生成章程',
    complete: '完成设置',
  },
} as const;