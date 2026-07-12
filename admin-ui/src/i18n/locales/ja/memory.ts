export const memory = {
    title: 'メモリブラウザ',
    actions: {
      refresh: 'リフレッシュ',
      triggerConsolidation: '統合の実行'
    },
    common: {
      notAvailable: '該当なし'
    },
    layers: {
      title: 'レイヤー',
      empty: 'メモリ層が見つかりません。',
      horizon: 'ホライズン',
      consolidationInterval: '統合間隔',
      retentionPolicy: '保存ポリシー',
      perspectiveCurrent: '現在の展望',
      perspectivePast: '過去の展望',
      perspectiveFuture: '将来の展望',
      viewObservations: '観測結果を見る'
    },
    consolidation: {
      title: '統合',
      activeJob: 'アクティブなジョブ',
      lastRun: 'ラストラン',
      lastJob: '最後の仕事',
      promoted: '昇格',
      demoted: '降格',
      archived: 'アーカイブ済み',
      state: {
        running: 'ランニング',
        idle: 'アイドル状態'
      }
    },
    list: {
      title: '観察',
      activeLayer: 'レイヤー: {layer}',
      sortBy: '並べ替え',
      orderBy: '注文',
      tagFilter: 'タグによるフィルター',
      pageSize: 'ページサイズ',
      compactMode: 'コンパクト',
      openDetail: '詳細',
      emptyLayer: 'この層にはまだ観測がありません。',
      emptyFilter: '現在のタグ フィルターに一致する観測はありません。',
      sort: {
        weight: '重量',
        age: '年齢',
        tags: 'タグ'
      },
      order: {
        desc: '降順',
        asc: '昇順'
      },
      columns: {
        content: '内容',
        tags: 'タグ',
        weight: '重量',
        age: '年齢',
        decay: '衰退',
        actions: 'アクション'
      }
    },
    detail: {
      title: '観測内容',
      empty: '検査および編集する観測を選択します。',
      id: 'ID',
      content: '内容',
      layer: 'レイヤー',

      timestamp: 'タイムスタンプ',
      demotionReason: '降格の理由',
      demotionTimestamp: '降格タイムスタンプ',
      editWeight: '重量',
      editTags: 'タグ',
      save: '保存',
      reset: 'リセット',
      archive: 'アーカイブ',
      saveSuccess: '観察を更新しました。',
      archiveSuccess: '観察はアーカイブされました。'
    }
  } as const;
