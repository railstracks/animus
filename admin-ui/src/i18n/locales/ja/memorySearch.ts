export const memorySearch = {
    title: 'メモリーサーチ',
    subtitle: 'すべてのデータ ドメインにわたってエージェントのメモリ クエリをテストします。エージェントが何を見つけるかを正確に確認します。',
    agentLabel: 'エージェント',
    queryLabel: '検索クエリ',
    searchButton: '検索',
    domainsLabel: 'データソース',
    domains: {
      episodic: 'エピソード記憶',
      semantic: '意味記憶',
      files: 'メモリファイル',
      sessions: 'セッション'
    },
    limitLabel: '結果の制限',
    resultsCount: '結果',
    noResults: '結果が見つかりませんでした。',
    placeholder: '検索クエリを入力してメモリの取得をテストします。',
    searching: '検索中...'
  } as const;
