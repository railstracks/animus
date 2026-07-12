export const activeMemory = {
    title: 'アクティブメモリ',
    subtitle: '組み立てられたエージェント コンテキスト - エージェントがそのプリアンブルに表示するもの',
    empty: 'エージェントを選択して、その組み立てられたコンテキストを表示します。',
    actions: {
      refresh: 'リフレッシュ'
    },
    labels: {
      agent: 'エージェント',
      session: 'セッション',
      syntheticSession: '合成 (テスト用の空のセッション)',
      blocks: 'ブロック'
    },
    flags: {
      synthetic: '合成セッション',
      live: 'ライブセッション'
    },
    sections: {
      rendered: 'レンダリングされた出力',
      blocks: 'ブロックの内訳'
    }
  } as const;
