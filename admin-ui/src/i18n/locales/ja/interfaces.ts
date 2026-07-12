export const interfaces = {
    title: 'インターフェース管理',
    actions: {
      refresh: '更新',
      add: 'インターフェースを追加',
      edit: '編集',
      enable: '有効化',
      disable: '無効化',
      delete: '削除',
      confirmDelete: 'インターフェース "{name}" を削除しますか？'
    },
    columns: {
      name: '名前',
      type: 'タイプ',
      module: 'モジュール ID',
      enabled: '有効',
      connected: '接続済み',
      lastEvent: '最後のイベント',
      actions: '操作'
    },
    state: {
      enabled: 'はい',
      disabled: 'いいえ',
      connected: '接続済み',
      disconnected: '未接続'
    },
    empty: 'まだインターフェースは設定されていません。',
    form: {
      createTitle: 'インターフェースを作成',
      editTitle: 'インターフェースを編集: {name}',
      name: 'インスタンス名',
      type: 'インターフェースタイプ',
      moduleId: 'モジュール ID',
      enabled: '有効',
      configJson: '設定 JSON',
      create: '作成',
      save: '保存',
      cancel: 'キャンセル',
      irc: {
        host: 'ホスト',
        port: 'ポート',
        nick: 'ニックネーム',
        serverPassword: 'サーバーパスワード',
        username: 'ユーザー名',
        realname: '実名',
        dmOnly: 'DM のみモード',
        respondToChannel: 'チャンネルアクティビティに応答',
        respondToDm: 'ダイレクトメッセージに応答',
        respondToNotices: '通知に応答',
        allowedDmUsers: '許可された DM ユーザー',
        allowedDmUsersHint: '任意の許可リスト。カンマまたは改行で区切ります。',
        agentId: 'エージェント ID',
        reconnectEnabled: '再接続を有効化',
        reconnectInitialDelay: '再接続の初期遅延（ミリ秒）',
        reconnectMaxDelay: '再接続の最大遅延（ミリ秒）'
      }
    },
    createSuccess: 'インターフェース "{name}" を作成しました。',
    updateSuccess: 'インターフェース "{name}" を更新しました。',
    deleteSuccess: 'インターフェース "{name}" を削除しました。'
  } as const;
