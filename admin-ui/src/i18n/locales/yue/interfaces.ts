export const interfaces = {
    title: '介面管理',
    actions: {
      refresh: '重新整理',
      add: '新增介面',
      edit: '編輯',
      enable: '啟用',
      disable: '停用',
      delete: '刪除',
      confirmDelete: '刪除介面「{name}」？'
    },
    columns: {
      name: '名稱',
      type: '類型',
      module: '模組 ID',
      enabled: '已啟用',
      connected: '已連線',
      lastEvent: '最後事件',
      actions: '操作'
    },
    state: {
      enabled: '係',
      disabled: '否',
      connected: '已連線',
      disconnected: '已斷線'
    },
    empty: '暫時未設定任何介面。',
    form: {
      createTitle: '建立介面',
      editTitle: '編輯介面：{name}',
      name: '實例名稱',
      type: '介面類型',
      moduleId: '模組 ID',
      enabled: '已啟用',
      configJson: '設定 JSON',
      create: '建立',
      save: '儲存',
      cancel: '取消',
      irc: {
        host: '主機',
        port: '連接埠',
        nick: '暱稱',
        serverPassword: '伺服器密碼',
        username: '使用者名稱',
        realname: '真實名稱',
        dmOnly: '只限私訊模式',
        respondToChannel: '回應頻道活動',
        respondToDm: '回應直接訊息',
        respondToNotices: '回應通知',
        allowedDmUsers: '允許私訊使用者',
        allowedDmUsersHint: '可選允許名單；用逗號或換行分隔。',
        agentId: '代理 ID',
        reconnectEnabled: '啟用重新連線',
        reconnectInitialDelay: '重新連線初始延遲（毫秒）',
        reconnectMaxDelay: '重新連線最大延遲（毫秒）'
      }
    },
    createSuccess: '介面「{name}」已建立。',
    updateSuccess: '介面「{name}」已更新。',
    deleteSuccess: '介面「{name}」已刪除。'
  } as const;
