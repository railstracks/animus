export const memory = {
    title: 'Bellek Tarayıcı',
    actions: {
      refresh: 'Yenile',
      triggerConsolidation: 'Konsolidasyonu Çalıştır'
    },
    common: {
      notAvailable: 'yok'
    },
    layers: {
      title: 'Katmanlar',
      empty: 'Bellek katmanı bulunamadı.',
      horizon: 'Ufuk',
      consolidationInterval: 'Konsolidasyon Aralığı',
      retentionPolicy: 'Saklama Politikası',
      perspectiveCurrent: 'Güncel Perspektif',
      perspectivePast: 'Geçmiş Perspektif',
      perspectiveFuture: 'Gelecek Perspektifi',
      viewObservations: 'Gözlemleri Görüntüle'
    },
    consolidation: {
      title: 'Konsolidasyon',
      activeJob: 'Aktif İş',
      lastRun: 'Son Çalıştırma',
      lastJob: 'Son İş',
      promoted: 'Tanıtıldı',
      demoted: 'Sırası düşürüldü',
      archived: 'Arşivlendi',
      state: {
        running: 'Koşu',
        idle: 'Boşta'
      }
    },
    list: {
      title: 'Gözlemler',
      activeLayer: 'Katman: {layer}',
      sortBy: 'Sırala',
      orderBy: 'Sipariş',
      tagFilter: 'Etiketlere Göre Filtrele',
      pageSize: 'Sayfa Boyutu',
      compactMode: 'Kompakt',
      openDetail: 'Ayrıntılar',
      emptyLayer: 'Bu katmanda henüz gözlem yok.',
      emptyFilter: 'Geçerli etiket filtresiyle eşleşen gözlem yok.',
      sort: {
        weight: 'Ağırlık',
        age: 'Yaş',
        tags: 'Etiketler'
      },
      order: {
        desc: 'Azalan',
        asc: 'Artan'
      },
      columns: {
        content: 'İçerik',
        tags: 'Etiketler',
        weight: 'Ağırlık',
        age: 'Yaş',
        decay: 'Çürüme',
        actions: 'Eylemler'
      }
    },
    detail: {
      title: 'Gözlem Detayı',
      empty: 'İncelemek ve düzenlemek için bir gözlem seçin.',
      id: 'Kimlik',
      content: 'İçerik',
      layer: 'Katman',

      timestamp: 'Zaman damgası',
      demotionReason: 'Düşüş Sebebi',
      demotionTimestamp: 'İndirgeme Zaman Damgası',
      editWeight: 'Ağırlık',
      editTags: 'Etiketler',
      save: 'Kaydet',
      reset: 'Sıfırla',
      archive: 'Arşiv',
      saveSuccess: 'Gözlem güncellendi.',
      archiveSuccess: 'Gözlem arşivlendi.'
    }
  } as const;
