export const channels = {
    title: '頻道',
    empty: '未配置通道。添加一個即可開始。',
    createSuccess: '頻道「{name}」建立成功。',
    updateSuccess: '頻道「{name}」更新成功。',
    deleteSuccess: '頻道「{name}」已刪除。',
    columns: {
      name: '姓名',
      type: '類型',
      identity: '身分',
      endpoint: '端點',
      enabled: '啟用',
      connected: '已連接',
      lastEvent: '最後活動',
      actions: '行動'
    },
    state: {
      connected: '已連接',
      disconnected: '已斷開連接'
    },
    actions: {
      refresh: '重新整理',
      add: '新增頻道',
      cancel: '取消',
      create: '創造',
      save: '節省',
      confirmDelete: '刪除頻道「{name}」？此操作無法撤銷。'
    },
    form: {
      createTitle: '新增頻道',
      editTitle: '編輯“{name}”',
      name: '頻道名稱',
      type: '通道類型',
      agent: '代理人',
      minResponseInterval: '最短回應間隔（秒）',
      allowInterjection: '允許插話',
      irc: {
        host: 'IRC伺服器',
        port: '港口',
        serverPassword: '伺服器密碼',
        nick: '暱稱',
        username: '使用者名稱',
        realname: '真實姓名',
        channels: '頻道',
        channelsHint: '每行一個。格式：#頻道[鍵]',
        agent: '代理人',
        respondToChannel: '回覆頻道訊息',
        respondToDm: '回覆直接訊息',
        respondToNotices: '回覆通知',
        allowedDmUsers: '允許的 DM 用戶',
        reconnect: '自動重新連接'
      },
      telegram: {
        botToken: '機器人代幣',
        tokenHint: '留空以保留現有令牌'
      },
      vk: {
        accessToken: '社區訪問令牌',
        groupId: '組號'
      },
      bluesky: {
        handle: '處理',
        appPassword: '應用程式密碼',
        pds: '商品資料服務網址'
      },
      mastodon: {
        handle: '處理',
        instance: '實例網址'
      },
      email: {
        apiKey: 'AgentMail API 金鑰',
        apiKeyHint: '留空以保留當前密鑰',
        inboxId: '收件匣ID',
        pollingWait: '輪詢間隔（秒）'
      },
      twitter: {
        tier: 'API層',
        clientId: 'OAuth 客戶端 ID',
        clientSecret: 'OAuth 用戶端秘密',
        accessToken: '訪問令牌',
        tokenHint: '留空以保留現有令牌',
        refreshToken: '刷新令牌',
        authorize: '透過 Twitter 授權',
        oauthStarted: '授權視窗打開。在瀏覽器中完成流程。'
      },
      discord: {
        botToken: '機器人代幣',
        tokenHint: '留空以保留現有令牌',
        applicationId: '應用程式 ID（用於斜線命令）',
        monitoredChannels: '追蹤頻道',
        monitoredChannelsHint: '用於上下文追蹤嘅頻道ID（每行一個）。訊息會被記錄但唔會觸發回覆。',
        respondToDm: '回覆 DM',
        respondToMentions: '回應提及',
        monitorAllChannels: '追蹤所有頻道',
        respondToChannels: '回覆追蹤頻道中嘅任何訊息',
        dmWhitelistEnabled: '將私信限制為允許嘅用戶',
        allowedDmUsers: '允許嘅私信用戶',
        allowedDmUsersHint: 'Discord用戶名（每行一個）。只有呢啲用戶可以向機械人發送私信。'
      },
      slack: {
        botToken: '機器人代幣 (xoxb-)',
        tokenHint: '留空以保留現有令牌',
        appToken: '應用程式令牌（xapp-）',
        appTokenHint: '對於套接字模式（第 2 階段）。第一階段可選。',
        monitoredChannels: '監控通道（覆蓋）',
        monitoredChannelsHint: '機器人會自動監控所屬的所有頻道。僅在此處新增 ID 以限制範圍。',
        respondToMentions: "回覆 {'@'} 提及",
        respondToAll: '回覆所有訊息（忽略提及過濾器）',
        threadedReplies: '頻道中的線程回應（每個訊息啟動一個線程）'
      }
    }
  } as const;
