export const ontology = {
    title: 'オントロジーエクスプローラー',
    subtitle: '意味論的エンティティとその観察に裏付けられたプロパティを参照します。',
    actions: {
      refresh: 'リフレッシュ'
    },
    sections: {
      agent: 'エージェント',
      categories: 'カテゴリー',
      entities: 'エンティティ',
      details: 'エンティティの詳細',
      properties: 'プロパティ'
    },
    states: {
      new: '新しい',
      current: '現在の',
      deprecated: '廃止された'
    },
    empty: {
      entities: 'このカテゴリのエンティティは見つかりませんでした。',
      details: 'プロパティを検査するエンティティを選択します。'
    }
  } as const;
