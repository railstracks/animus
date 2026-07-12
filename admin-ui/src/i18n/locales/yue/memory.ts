export const memory = {
    title: '記憶體瀏覽器',
    actions: {
      refresh: '重新整理',
      triggerConsolidation: '運行整合'
    },
    common: {
      notAvailable: '不適用'
    },
    layers: {
      title: '層數',
      empty: '未找到內存層。',
      horizon: '地平線',
      consolidationInterval: '鞏固區間',
      retentionPolicy: '保留政策',
      perspectiveCurrent: '當前觀點',
      perspectivePast: '過去的觀點',
      perspectiveFuture: '未來展望',
      viewObservations: '查看觀察結果'
    },
    consolidation: {
      title: '合併',
      activeJob: '活躍工作',
      lastRun: '最後一次運行',
      lastJob: '上一份工作',
      promoted: '晉升',
      demoted: '降職',
      archived: '已存檔',
      state: {
        running: '跑步',
        idle: '閒置的'
      }
    },
    list: {
      title: '觀察結果',
      activeLayer: '圖層：{layer}',
      sortBy: '種類',
      orderBy: '命令',
      tagFilter: '按標籤過濾',
      pageSize: '頁面尺寸',
      compactMode: '袖珍的',
      openDetail: '細節',
      emptyLayer: '該層尚未有任何觀測結果。',
      emptyFilter: '沒有觀察結果與目前標籤過濾器相符。',
      sort: {
        weight: '重量',
        age: '年齡',
        tags: '標籤'
      },
      order: {
        desc: '降序',
        asc: '升序'
      },
      columns: {
        content: '內容',
        tags: '標籤',
        weight: '重量',
        age: '年齡',
        decay: '衰變',
        actions: '行動'
      }
    },
    detail: {
      title: '觀察細節',
      empty: '選擇要檢查和編輯的觀察結果。',
      id: 'ID',
      content: '內容',
      layer: '層',

      timestamp: '時間戳',
      demotionReason: '降級原因',
      demotionTimestamp: '降級時間戳',
      editWeight: '重量',
      editTags: '標籤',
      save: '節省',
      reset: '重置',
      archive: '檔案',
      saveSuccess: '觀察已更新。',
      archiveSuccess: '觀察存檔。'
    }
  } as const;
