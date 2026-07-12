export const memory = {
    title: '内存浏览器',
    actions: {
      refresh: '刷新',
      triggerConsolidation: '运行整合'
    },
    common: {
      notAvailable: '不适用'
    },
    layers: {
      title: '层数',
      empty: '未找到内存层。',
      horizon: '地平线',
      consolidationInterval: '巩固区间',
      retentionPolicy: '保留政策',
      perspectiveCurrent: '当前观点',
      perspectivePast: '过去的观点',
      perspectiveFuture: '未来展望',
      viewObservations: '查看观察结果'
    },
    consolidation: {
      title: '整合',
      activeJob: '活跃工作',
      lastRun: '最后一次运行',
      lastJob: '上一份工作',
      promoted: '晋升',
      demoted: '降职',
      archived: '已存档',
      state: {
        running: '跑步',
        idle: '空闲'
      }
    },
    list: {
      title: '观察结果',
      activeLayer: '图层：{layer}',
      sortBy: '排序',
      orderBy: '订单',
      tagFilter: '按标签过滤',
      pageSize: '页面尺寸',
      compactMode: '紧凑型',
      openDetail: '详情',
      emptyLayer: '该层尚未有任何观测结果。',
      emptyFilter: '没有观察结果与当前标签过滤器匹配。',
      sort: {
        weight: '重量',
        age: '年龄',
        tags: '标签'
      },
      order: {
        desc: '降序',
        asc: '升序'
      },
      columns: {
        content: '内容',
        tags: '标签',
        weight: '重量',
        age: '年龄',
        decay: '腐烂',
        actions: '行动'
      }
    },
    detail: {
      title: '观察细节',
      empty: '选择要检查和编辑的观察结果。',
      id: '身份证号',
      content: '内容',
      layer: '图层',

      timestamp: '时间戳',
      demotionReason: '降级原因',
      demotionTimestamp: '降级时间戳',
      editWeight: '重量',
      editTags: '标签',
      save: '保存',
      reset: '重置',
      archive: '存档',
      saveSuccess: '观察已更新。',
      archiveSuccess: '观察存档。'
    }
  } as const;
