export const channels = {
    title: 'チャンネル',
    empty: 'チャネルが設定されていません。開始するには 1 つ追加してください。',
    createSuccess: 'チャンネル「{name}」が正常に作成されました。',
    updateSuccess: 'チャンネル「{name}」が正常に更新されました。',
    deleteSuccess: 'チャンネル「{name}」が削除されました。',
    columns: {
      name: '名前',
      type: 'タイプ',
      identity: 'アイデンティティ',
      endpoint: 'エンドポイント',
      enabled: '有効',
      connected: '接続済み',
      lastEvent: '最後のイベント',
      actions: 'アクション'
    },
    state: {
      connected: '接続済み',
      disconnected: '切断されました'
    },
    actions: {
      refresh: 'リフレッシュ',
      add: 'チャンネルの追加',
      cancel: 'キャンセル',
      create: '作成',
      save: '保存',
      confirmDelete: 'チャンネル「{name}」を削除しますか?これを元に戻すことはできません。'
    },
    form: {
      createTitle: 'チャンネルの追加',
      editTitle: '「{name}」を編集',
      name: 'チャンネル名',
      type: 'チャンネルタイプ',
      agent: 'エージェント',
      minResponseInterval: '最小応答間隔（秒）',
      allowInterjection: '割り込みを許可',
      irc: {
        host: 'IRCサーバー',
        port: '港',
        serverPassword: 'サーバーのパスワード',
        nick: 'ニックネーム',
        username: 'ユーザー名',
        realname: '本名',
        channels: 'チャンネル',
        channelsHint: '1 行に 1 つ。形式: #チャンネル[キー]',
        agent: 'エージェント',
        respondToChannel: 'チャンネルメッセージに応答する',
        respondToDm: 'ダイレクトメッセージに返信する',
        respondToNotices: '通知に応答する',
        allowedDmUsers: '許可されたDMユーザー',
        reconnect: '自動再接続'
      },
      telegram: {
        botToken: 'ボットトークン',
        tokenHint: '既存のトークンを保持するには空白のままにします'
      },
      vk: {
        accessToken: 'コミュニティアクセストークン',
        groupId: 'グループID'
      },
      bluesky: {
        handle: 'ハンドル',
        appPassword: 'アプリのパスワード',
        pds: 'PDS URL'
      },
      mastodon: {
        handle: 'ハンドル',
        instance: 'インスタンスURL'
      },
      email: {
        apiKey: 'AgentMail API キー',
        apiKeyHint: '現在のキーを保持するには空のままにしておきます',
        inboxId: '受信箱ID',
        pollingWait: 'ポーリング間隔 (秒)'
      },
      twitter: {
        tier: 'API層',
        clientId: 'OAuth クライアント ID',
        clientSecret: 'OAuth クライアント シークレット',
        accessToken: 'アクセストークン',
        tokenHint: '既存のトークンを保持するには空白のままにします',
        refreshToken: 'リフレッシュトークン',
        authorize: 'Twitterで認証する',
        oauthStarted: '認証ウィンドウが開きました。ブラウザでフローを完了します。'
      },
      discord: {
        botToken: 'ボットトークン',
        tokenHint: '既存のトークンを保持するには空白のままにします',
        applicationId: 'アプリケーションID (スラッシュコマンド用)',
        monitoredChannels: '追跡チャンネル',
        monitoredChannelsHint: 'コンテキスト用に追跡するチャンネルID（1行に1つ）。メッセージは応答をトリガーせずに記録されます。',
        respondToDm: 'DMに返信する',
        respondToMentions: 'メンションに応答する',
        monitorAllChannels: 'すべてのチャンネルを追跡',
        respondToChannels: '追跡チャンネルのすべてのメッセージに応答',
        dmWhitelistEnabled: 'DMを許可されたユーザーに制限',
        allowedDmUsers: '許可されたDMユーザー',
        allowedDmUsersHint: 'Discordユーザー名（1行に1つ）。これらのユーザーのみがボットにDMを送信できます。'
      },
      slack: {
        botToken: 'ボットトークン (xoxb-)',
        tokenHint: '既存のトークンを保持するには空白のままにします',
        appToken: 'アプリトークン (xapp-)',
        appTokenHint: 'ソケット モード (フェーズ 2) の場合。フェーズ 1 のオプション。',
        monitoredChannels: '監視対象チャネル (オーバーライド)',
        monitoredChannelsHint: 'ボットは、メンバーとなっているすべてのチャネルを自動監視します。ここでは範囲を制限するために ID のみを追加します。',
        respondToMentions: "{'@'}メンションに返信する",
        respondToAll: 'すべてのメッセージに応答します (メンション フィルターを無視します)',
        threadedReplies: 'チャネル内のスレッド応答 (各メッセージがスレッドを開始します)'
      }
    }
  } as const;
