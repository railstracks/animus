export const memoryFiles = {
    title: '内存文件',
    subtitle: '存储原始文本工件并跨内存域进行搜索。',
    common: {
      yes: '是的',
      no: '不'
    },
    actions: {
      refresh: '刷新',
      open: '打开',
      delete: '删除',
      process: '队列处理',
      import: '进口',
      importBatch: '导入批次',
      saveMetadata: '保存元数据',
      search: '搜索'
    },
    fields: {
      sourcePath: '源路径',
      fileType: '文件类型',
      contentMutable: '内容可变',
      agentId: '代理 ID',
      status: '状态',
      agentFilter: '按代理过滤',
      allAgents: '所有代理商',
      superseded: '取代',
      content: '内容'
    },
    types: {
      all: '所有类型',
      expanded_memory: '扩展内存',
      session_log: '会话日志',
      daily_note: '每日笔记',
      bootstrap_file: '引导文件',
      journal: '期刊',
      external_doc: '外部文档'
    },
    stats: {
      title: '统计数据'
    },
    list: {
      title: '文件列表',
      typeFilter: '类型过滤器',
      limit: '限制',
      offset: '偏移量',
      columns: {
        id: '身份证号',
        type: '类型',
        sourcePath: '源路径',
        contentMutable: '可变的',
        agentId: '代理 ID',
      status: '状态',
      agentFilter: '按代理过滤',
      allAgents: '所有代理商',
        superseded: '取代',
        importedAt: '进口',
        actions: '行动'
      },
      status: {
        unprocessed: '未加工的',
        processed: '已加工'
      }
    },
    import: {
      singleTitle: '导入文件',
      selectFile: '选择文件',
      selected: '已选择',
      batchTitle: '批量导入',
      selectFiles: '选择文件',
      filesSelected: '选定的文件'
    },
    detail: {
      title: '文件详细信息',
      empty: '从列表中选择一个文件以检查和编辑元数据。',
      createdAt: '已创建',
      importedAt: '进口',
      contentTitle: '内容',
      contentImmutableNotice: '内容编辑被禁用，因为该文件被标记为不可变。'
    },
    search: {
      title: '统一搜索',
      query: '搜索查询',
      limit: '限制',
      relevance: '相关性',
      empty: '还没有搜索结果。',
      domains: {
        observation: '观察',
        observations: '观察结果',
        ontology: '本体论',
        memory_file: '文件',
        sessions: '会议'
      }
    },
    errors: {
      loadFiles: '无法加载内存文件。',
      loadDetail: '无法加载文件详细信息。',
      importRequired: '选择要导入的文件。',
      importSingle: '内存文件导入失败。',
      batchRequired: '至少选择一个文件进行批量导入。',
      importBatch: '导入批量负载失败。',
      updateMetadata: '无法更新元数据。',
      delete: '删除内存文件失败。',
      search: '内存搜索失败。'
    }
  } as const;
