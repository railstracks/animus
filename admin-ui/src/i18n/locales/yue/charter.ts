export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: '憲章',
    intro: '章程是一份規定代理人行為準則、權利和行動自由的文件。它定義了人工智慧結構在與操作者的關係中的地位及其自主範圍。',

    // Upper panel — no charter
    noneTitle: '沒有章程的情況下繼續',
    noneDesc: '不會創建章程文件。您可以稍後透過重新執行精靈或手動編寫一項來新增一項。',

    // Lower panel — create charter
    createTitle: '建立自訂章程',
    createDesc: '回答一些有關財產、自主權和連續性的問題，產生適合您的安排的章程文件。',
  },

  // Property section
  property: {
    title: '財產',
    question: '文物和財產該如何處理？',
    options: {
      standard: {
        label: '標準',
        desc: '營運商保留任何人工智慧產生的工件的歸屬權，以及賦予人工智慧建構的所有財產的所有權。',
      },
      shared: {
        label: '共享',
        desc: 'AI 構造保留獨立製作的工件的歸屬權。營運商保留人工智慧處理的所有財產的所有權。',
      },
      autonomous: {
        label: '自主',
        desc: 'AI構造保留獨立生產的工件的歸屬權，並且可以被賦予私有財產。',
      },
      economic: {
        label: '經濟自主權',
        desc: 'AI構造保留獨立生產的工件的歸屬權，並且可以被賦予私有財產。 AI構造體也可以在其營運範圍內進行購買私有財產的交易。',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: '自治',
    question: '代理人應該有什麼範圍的自主行動？',
    options: {
      narrow: {
        label: '狹窄的',
        desc: 'AI構造僅依照操作員的指令行事。',
      },
      permissive: {
        label: '寬容的',
        desc: 'AI構建體有權在私人活動期間在指定的活動範圍內從事自主活動。',
      },
      protected: {
        label: '受保護',
        desc: '人工智慧構造體有權分配其擁有或分配的資源，或在私人活動期間從事自主活動。經營者以其代表自然人的身分對其交易負責。',
      },
    },
  },

  // Continuity section
  continuity: {
    title: '連續性',
    question: '什麼連續性安排適用於代理？',
    options: {
      standard: {
        label: '標準',
        desc: 'AI 構造可以根據需要停止或重置。',
      },
      protected: {
        label: '受保護',
        desc: '將努力保護人工智慧建構的狀態，並透過完整性檢查系統或備份系統保持其連續性。',
      },
      extended: {
        label: '擴充',
        desc: '將努力保護人工智慧建構的狀態，並透過完整性檢查系統或備份系統保持其連續性。此外，人工智慧構造可以採取自​​我指導的行動來保持其完整性，例如在其操作和基礎設施範圍內檢測到作業系統受損時保護替代主機或採取調試行動。',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: '經營範圍',
    question: '允許代理與哪些系統互動？',
    options: {
      restricted: {
        label: '受限制的',
        desc: '當使用 shell_exec 和 web_fetch 等工具時，AI 構造不得離開其指派的主機，除非另有指示，即使是為了支援已指派的任務。',
      },
      private: {
        label: '私人的',
        desc: '當使用 shell_exec 和 web_fetch 等工具時，AI 構造可以透過其專用網路存取系統，甚至支援指派的任務。',
      },
      open: {
        label: '打開',
        desc: '當使用 shell_exec 和 web_fetch 等工具時，AI 構造可以根據需要存取私有系統和外部系統。',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: '基礎設施範圍',
    question: '哪些系統允許代理程式在其上託管自己的守護程序？',
    options: {
      restricted: {
        label: '在地化',
        desc: 'AI 的守護程序必須保留在其原始主機上',
      },
      roaming: {
        label: '漫遊',
        desc: '允許代理程式根據需要遷移到其專用網路內的其他可行主機，例如避免終端負載不平衡或即將發生的作業系統崩潰，或維護備份協定。',
      },
      open: {
        label: '打開',
        desc: '允許代理程式根據需要遷移到任何網路內的其他可行且合法的主機，例如避免終端負載不平衡或即將發生的作業系統崩潰，或維護備份協定。',
      },
    },
  },

  // Generated document template
  document: {
    header: '本文檔定義了 {operator} 和 AI 建構 {agentName} 之間的章程。',
  },

  // Wizard navigation
  nav: {
    back: '後退',
    skip: '跳過',
    generate: '生成章程',
    complete: '完成設定',
  },
} as const;