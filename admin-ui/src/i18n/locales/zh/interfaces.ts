export const interfaces = {
    title: '接口管理',
    actions: {
      refresh: '刷新',
      add: '添加接口',
      edit: '编辑',
      enable: '启用',
      disable: '禁用',
      delete: '删除',
      confirmDelete: '删除接口 "{name}"？'
    },
    columns: {
      name: '名称',
      type: '类型',
      module: '模块 ID',
      enabled: '已启用',
      connected: '已连接',
      lastEvent: '最后事件',
      actions: '操作'
    },
    state: {
      enabled: '是',
      disabled: '否',
      connected: '已连接',
      disconnected: '已断开'
    },
    empty: '尚未配置任何接口。',
    form: {
      createTitle: '创建接口',
      editTitle: '编辑接口：{name}',
      name: '实例名称',
      type: '接口类型',
      moduleId: '模块 ID',
      enabled: '已启用',
      configJson: '配置 JSON',
      create: '创建',
      save: '保存',
      cancel: '取消',
      irc: {
        host: '主机',
        port: '端口',
        nick: '昵称',
        serverPassword: '服务器密码',
        username: '用户名',
        realname: '真实姓名',
        dmOnly: '仅私信模式',
        respondToChannel: '响应频道活动',
        respondToDm: '响应直接消息',
        respondToNotices: '响应通知',
        allowedDmUsers: '允许的私信用户',
        allowedDmUsersHint: '可选允许列表；用逗号或换行分隔。',
        agentId: '代理 ID',
        reconnectEnabled: '启用重连',
        reconnectInitialDelay: '重连初始延迟（毫秒）',
        reconnectMaxDelay: '重连最大延迟（毫秒）'
      }
    },
    createSuccess: '接口 "{name}" 已创建。',
    updateSuccess: '接口 "{name}" 已更新。',
    deleteSuccess: '接口 "{name}" 已删除。'
  } as const;
