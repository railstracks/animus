export const memoryFiles = {
    title: 'メモリファイル',
    subtitle: '生のテキスト アーティファクトを保存し、メモリ ドメイン全体を検索します。',
    common: {
      yes: 'はい',
      no: 'いいえ'
    },
    actions: {
      refresh: 'リフレッシュ',
      open: '開く',
      delete: '削除',
      process: '処理のキュー',
      import: 'インポート',
      importBatch: 'インポートバッチ',
      saveMetadata: 'メタデータの保存',
      search: '検索'
    },
    fields: {
      sourcePath: 'ソースパス',
      fileType: 'ファイルの種類',
      contentMutable: 'コンテンツ変更可能',
      agentId: 'エージェントID',
      status: 'ステータス',
      agentFilter: 'エージェントによるフィルター',
      allAgents: 'すべてのエージェント',
      superseded: '置き換えられました',
      content: '内容'
    },
    types: {
      all: '全種類',
      expanded_memory: '拡張メモリ',
      session_log: 'セッションログ',
      daily_note: '日々のメモ',
      bootstrap_file: 'ブートストラップファイル',
      journal: '日記',
      external_doc: '外部ドキュメント'
    },
    stats: {
      title: '統計'
    },
    list: {
      title: 'ファイルリスト',
      typeFilter: 'タイプフィルター',
      limit: '限界',
      offset: 'オフセット',
      columns: {
        id: 'ID',
        type: 'タイプ',
        sourcePath: 'ソースパス',
        contentMutable: '可変',
        agentId: 'エージェントID',
      status: 'ステータス',
      agentFilter: 'エージェントによるフィルター',
      allAgents: 'すべてのエージェント',
        superseded: '置き換えられました',
        importedAt: '輸入品',
        actions: 'アクション'
      },
      status: {
        unprocessed: '未加工',
        processed: '加工済み'
      }
    },
    import: {
      singleTitle: 'インポートファイル',
      selectFile: 'ファイルを選択',
      selected: '選択済み',
      batchTitle: 'バッチインポート',
      selectFiles: 'ファイルの選択',
      filesSelected: '選択されたファイル'
    },
    detail: {
      title: 'ファイルの詳細',
      empty: 'リストからファイルを選択して、メタデータを検査および編集します。',
      createdAt: '作成されました',
      importedAt: '輸入品',
      contentTitle: '内容',
      contentImmutableNotice: 'このファイルは不変としてマークされているため、コンテンツの編集は無効になっています。'
    },
    search: {
      title: '統合検索',
      query: '検索クエリ',
      limit: '限界',
      relevance: '関連性',
      empty: 'まだ検索結果がありません。',
      domains: {
        observation: '観察',
        observations: '観察',
        ontology: 'オントロジー',
        memory_file: 'ファイル',
        sessions: 'セッション'
      }
    },
    errors: {
      loadFiles: 'メモリファイルのロードに失敗しました。',
      loadDetail: 'ファイル詳細の読み込みに失敗しました。',
      importRequired: 'インポートするファイルを選択します。',
      importSingle: 'メモリファイルのインポートに失敗しました。',
      batchRequired: 'バッチインポートするファイルを少なくとも 1 つ選択します。',
      importBatch: 'バッチ ペイロードのインポートに失敗しました。',
      updateMetadata: 'メタデータの更新に失敗しました。',
      delete: 'メモリファイルの削除に失敗しました。',
      search: 'メモリ検索に失敗しました。'
    }
  } as const;
