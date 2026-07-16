export const channels = {
    title: '渠道',
    empty: '未配置通道。添加一个即可开始。',
    createSuccess: '频道“{name}”创建成功。',
    updateSuccess: '频道“{name}”更新成功。',
    deleteSuccess: '频道“{name}”已删除。',
    columns: {
      name: '名称',
      type: '类型',
      identity: '身份',
      endpoint: '端点',
      enabled: '启用',
      connected: '已连接',
      lastEvent: '最后活动',
      actions: '行动'
    },
    state: {
      connected: '已连接',
      disconnected: '已断开连接'
    },
    actions: {
      refresh: '刷新',
      add: '添加频道',
      cancel: '取消',
      create: '创建',
      save: '保存',
      confirmDelete: '删除频道“{name}”？此操作无法撤消。'
    },
    form: {
      createTitle: '添加频道',
      editTitle: '编辑“{name}”',
      name: '频道名称',
      type: '通道类型',
      agent: '代理',
      minResponseInterval: '最短回复间隔（秒）',
      allowInterjection: '允许插话',
      irc: {
        host: 'IRC服务器',
        port: '港口',
        serverPassword: '服务器密码',
        nick: '昵称',
        username: '用户名',
        realname: '真实姓名',
        channels: '渠道',
        channelsHint: '每行一个。格式：#频道[键]',
        agent: '代理',
        respondToChannel: '回复频道消息',
        respondToDm: '回复直接消息',
        respondToNotices: '回复通知',
        allowedDmUsers: '允许的 DM 用户',
        reconnect: '自动重新连接'
      },
      telegram: {
        botToken: '机器人代币',
        tokenHint: '留空以保留现有令牌'
      },
      vk: {
        accessToken: '社区访问令牌',
        groupId: '组号'
      },
      bluesky: {
        handle: '手柄',
        appPassword: '应用程序密码',
        pds: '商品数据服务网址'
      },
      mastodon: {
        handle: '手柄',
        instance: '实例网址'
      },
      email: {
        apiKey: 'AgentMail API 密钥',
        apiKeyHint: '留空以保留当前密钥',
        inboxId: '收件箱ID',
        pollingWait: '轮询间隔（秒）'
      },
      twitter: {
        tier: 'API层',
        clientId: 'OAuth 客户端 ID',
        clientSecret: 'OAuth 客户端秘密',
        accessToken: '访问令牌',
        tokenHint: '留空以保留现有令牌',
        refreshToken: '刷新令牌',
        authorize: '通过 Twitter 授权',
        oauthStarted: '授权窗口打开。在浏览器中完成流程。'
      },
      discord: {
        botToken: '机器人代币',
        tokenHint: '留空以保留现有令牌',
        applicationId: '应用程序 ID（用于斜杠命令）',
        monitoredChannels: '监控频道',
        monitoredChannelsHint: '每行一个通道 ID。机器人必须位于服务器中。',
        respondToDm: '回复 DM',
        respondToMentions: '回应提及',
        dmWhitelistEnabled: '将私信限制为允许的用户',
        allowedDmUsers: '允许的私信用户',
        allowedDmUsersHint: 'Discord用户名（每行一个）。只有这些用户可以向机器人发送私信。'
      },
      slack: {
        botToken: '机器人令牌 (xoxb-)',
        tokenHint: '留空以保留现有令牌',
        appToken: '应用程序令牌（xapp-）',
        appTokenHint: '对于套接字模式（第 2 阶段）。第一阶段可选。',
        monitoredChannels: '监控通道（覆盖）',
        monitoredChannelsHint: '机器人自动监控其所属的所有频道。仅在此处添加 ID 以限制范围。',
        respondToMentions: "回复 {'@'} 提及",
        respondToAll: '回复所有消息（忽略提及过滤器）',
        threadedReplies: '频道中的线程回复（每条消息启动一个线程）'
      }
    }
  } as const;
