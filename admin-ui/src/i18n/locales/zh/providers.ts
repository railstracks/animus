export const providers = {
    title: '提供商配置',
    actions: {
      refresh: '刷新',
      add: '添加提供商',
      test: '测试',
      edit: '编辑',
      delete: '删除',
      setDefault: '设置默认值',
      confirmDelete: '删除提供商“{id}”？此操作无法撤消。'
    },
    columns: {
      status: '状态',
      provider: '名称',
      type: '类型',
      baseUrl: '基本网址',
      defaultModel: '型号',
      default: '默认',
      actions: '行动'
    },
    empty: {
      title: '无提供商',
      description: '添加 LLM 提供商以开始使用。'
    },
    form: {
      createTitle: '添加提供商',
      editTitle: '编辑提供商',
      providerType: '提供商类型',
      instanceName: '实例名称',
      instanceNameHint: '此提供程序配置的唯一名称。',
      baseUrl: '基本网址',
      defaultModel: '默认型号',
      defaultContextWindow: '默认上下文窗口',
      modelContextWindows: '每个模型的上下文覆盖',
      modelContextWindowsHint: 'JSON 对象，例如gpt-5.2：200000',
      modelContextWindowsInvalid: '每个模型上下文覆盖必须是有效的 JSON。',
      authType: '验证类型',
      apiKey: 'API密钥',
      apiKeyPlaceholder: '留空以保留当前密钥',
      authFile: '验证文件',
      oauthBrowserTitle: '浏览器登录（授权码）',
      oauthRedirectUri: '重定向URI',
      oauthStartBrowser: '开始浏览器登录',
      oauthAuthorizationUrl: '授权网址',
      oauthState: '状态',
      oauthCallbackUrl: '粘贴回调 URL',
      oauthCallbackHint: '粘贴包含代码和状态的完整回调 URL。',
      oauthCompleteBrowser: '完成浏览器登录',
      concurrency: '最大并发数',
      create: '创建',
      save: '保存',
      cancel: '取消',
      modelManualHint: '手动输入型号 ID。型号列表不适用于此提供商类型。',
      modelsLockedHint: '使用 API 密钥保存提供程序以解锁模型选择。',
      savedContinue: '提供商已保存。您现在可以选择一个模型。',
      saveAndContinue: '保存并继续'
    },
    testSuccess: '提供者“{id}”可以访问。',
    testFailed: '提供程序“{id}”测试失败',
    createSuccess: '提供程序“{id}”已创建。',
    updateSuccess: '提供商“{id}”已更新。',
    deleteSuccess: '提供商“{id}”已删除。',
    defaultSuccess: '默认提供程序设置为“{id}”。',
    errors: {
      loadFailed: '无法加载提供商'
    }
  } as const;
