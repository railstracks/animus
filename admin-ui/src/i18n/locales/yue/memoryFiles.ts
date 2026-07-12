export const memoryFiles = {
    title: '記憶體檔案',
    subtitle: '儲存原始文字工件並跨記憶體域進行搜尋。',
    common: {
      yes: '是的',
      no: '不'
    },
    actions: {
      refresh: '重新整理',
      open: '打開',
      delete: '刪除',
      process: '佇列處理',
      import: '進口',
      importBatch: '導入批次',
      saveMetadata: '儲存元數據',
      search: '搜尋'
    },
    fields: {
      sourcePath: '來源路徑',
      fileType: '文件類型',
      contentMutable: '內容可變',
      agentId: '代理 ID',
      status: '地位',
      agentFilter: '按代理過濾',
      allAgents: '所有代理商',
      superseded: '取代',
      content: '內容'
    },
    types: {
      all: '所有類型',
      expanded_memory: '擴充記憶體',
      session_log: '會話日誌',
      daily_note: '每日筆記',
      bootstrap_file: '引導檔案',
      journal: '雜誌',
      external_doc: '外部文檔'
    },
    stats: {
      title: '統計數據'
    },
    list: {
      title: '文件列表',
      typeFilter: '類型過濾器',
      limit: '限制',
      offset: '抵銷',
      columns: {
        id: 'ID',
        type: '類型',
        sourcePath: '來源路徑',
        contentMutable: '可變的',
        agentId: '代理 ID',
      status: '地位',
      agentFilter: '按代理過濾',
      allAgents: '所有代理商',
        superseded: '取代',
        importedAt: '進口',
        actions: '行動'
      },
      status: {
        unprocessed: '未加工的',
        processed: '已加工'
      }
    },
    import: {
      singleTitle: '導入文件',
      selectFile: '選擇文件',
      selected: '已選擇',
      batchTitle: '批次導入',
      selectFiles: '選擇文件',
      filesSelected: '選定的文件'
    },
    detail: {
      title: '文件詳細信息',
      empty: '從清單中選擇一個檔案以檢查和編輯元資料。',
      createdAt: '已創建',
      importedAt: '進口',
      contentTitle: '內容',
      contentImmutableNotice: '內容編輯被停用，因為該檔案被標記為不可變。'
    },
    search: {
      title: '統一搜尋',
      query: '搜尋查詢',
      limit: '限制',
      relevance: '關聯',
      empty: '還沒有搜尋結果。',
      domains: {
        observation: '觀察',
        observations: '觀察結果',
        ontology: '本體論',
        memory_file: '文件',
        sessions: '會議'
      }
    },
    errors: {
      loadFiles: '無法載入記憶體檔案。',
      loadDetail: '無法載入文件詳細資料。',
      importRequired: '選擇要匯入的檔案。',
      importSingle: '內存檔案導入失敗。',
      batchRequired: '至少選擇一個文件進行批次匯入。',
      importBatch: '匯入批次負載失敗。',
      updateMetadata: '無法更新元資料。',
      delete: '刪除記憶體檔案失敗。',
      search: '記憶體搜尋失敗。'
    }
  } as const;
