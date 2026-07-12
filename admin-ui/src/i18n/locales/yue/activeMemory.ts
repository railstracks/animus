export const activeMemory = {
    title: '主動記憶',
    subtitle: '組裝的代理上下文 - 代理在其序言中看到的內容',
    empty: '選擇一個代理程式以查看其組裝的上下文。',
    actions: {
      refresh: '重新整理'
    },
    labels: {
      agent: '代理人',
      session: '會議',
      syntheticSession: '合成（用於測試的空會話）',
      blocks: '區塊'
    },
    flags: {
      synthetic: '綜合會議',
      live: '現場會議'
    },
    sections: {
      rendered: '渲染輸出',
      blocks: '區塊細分'
    }
  } as const;
