export const memoryFiles = {
    title: 'Bellek Dosyaları',
    subtitle: 'Ham metin yapıtlarını depolayın ve bellek alanlarında arama yapın.',
    common: {
      yes: 'evet',
      no: 'hayır'
    },
    actions: {
      refresh: 'Yenile',
      open: 'Açık',
      delete: 'Sil',
      process: 'İşleme Sırası',
      import: 'İthalat',
      importBatch: 'Toplu İçe Aktarma',
      saveMetadata: 'Meta Verileri Kaydet',
      search: 'Ara'
    },
    fields: {
      sourcePath: 'Kaynak Yolu',
      fileType: 'Dosya Türü',
      contentMutable: 'İçerik Değiştirilebilir',
      agentId: 'Temsilci Kimliği',
      status: 'Durum',
      agentFilter: 'Temsilciye göre filtrele',
      allAgents: 'Tüm Temsilciler',
      superseded: 'Değiştirildi',
      content: 'İçerik'
    },
    types: {
      all: 'Tüm Türler',
      expanded_memory: 'Genişletilmiş Bellek',
      session_log: 'Oturum Günlüğü',
      daily_note: 'Günlük Not',
      bootstrap_file: 'Önyükleme Dosyası',
      journal: 'Günlük',
      external_doc: 'Harici Belge'
    },
    stats: {
      title: 'İstatistikler'
    },
    list: {
      title: 'Dosya Listesi',
      typeFilter: 'Tip Filtresi',
      limit: 'Sınır',
      offset: 'Ofset',
      columns: {
        id: 'Kimlik',
        type: 'Tür',
        sourcePath: 'Kaynak Yolu',
        contentMutable: 'Değişken',
        agentId: 'Temsilci Kimliği',
      status: 'Durum',
      agentFilter: 'Temsilciye göre filtrele',
      allAgents: 'Tüm Temsilciler',
        superseded: 'Değiştirildi',
        importedAt: 'İthal',
        actions: 'Eylemler'
      },
      status: {
        unprocessed: 'işlenmemiş',
        processed: 'İşlendi'
      }
    },
    import: {
      singleTitle: 'Dosyayı İçe Aktar',
      selectFile: 'Dosya seç',
      selected: 'Seçildi',
      batchTitle: 'Toplu İçe Aktarma',
      selectFiles: 'Dosyaları seçin',
      filesSelected: 'seçilen dosyalar'
    },
    detail: {
      title: 'Dosya Detayı',
      empty: 'Meta verileri incelemek ve düzenlemek için listeden bir dosya seçin.',
      createdAt: 'Oluşturuldu',
      importedAt: 'İthal',
      contentTitle: 'İçerik',
      contentImmutableNotice: 'Bu dosya değişmez olarak işaretlendiğinden içerik düzenleme devre dışı bırakıldı.'
    },
    search: {
      title: 'Birleşik Arama',
      query: 'Arama Sorgusu',
      limit: 'Sınır',
      relevance: 'Alaka düzeyi',
      empty: 'Henüz arama sonucu yok.',
      domains: {
        observation: 'Gözlem',
        observations: 'Gözlemler',
        ontology: 'Ontoloji',
        memory_file: 'Dosyalar',
        sessions: 'Oturumlar'
      }
    },
    errors: {
      loadFiles: 'Bellek dosyaları yüklenemedi.',
      loadDetail: 'Dosya ayrıntısı yüklenemedi.',
      importRequired: 'İçe aktarılacak bir dosya seçin.',
      importSingle: 'Bellek dosyası içe aktarılamadı.',
      batchRequired: 'Toplu içe aktarma için en az bir dosya seçin.',
      importBatch: 'Toplu veri içe aktarılamadı.',
      updateMetadata: 'Meta veriler güncellenemedi.',
      delete: 'Bellek dosyası silinemedi.',
      search: 'Bellek araması başarısız oldu.'
    }
  } as const;
