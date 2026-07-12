export const providers = {
    title: 'Sağlayıcı Yapılandırması',
    actions: {
      refresh: 'Yenile',
      add: 'Sağlayıcı Ekle',
      test: 'testi',
      edit: 'Düzenle',
      delete: 'Sil',
      setDefault: 'Varsayılanı Ayarla',
      confirmDelete: '"{id}" sağlayıcısı kaldırılsın mı? Bu geri alınamaz.'
    },
    columns: {
      status: 'Durum',
      provider: 'İsim',
      type: 'Tür',
      baseUrl: 'Temel URL',
      defaultModel: 'Modeli',
      default: 'Varsayılan',
      actions: 'Eylemler'
    },
    empty: {
      title: 'Sağlayıcı Yok',
      description: 'Başlamak için bir LLM sağlayıcısı ekleyin.'
    },
    form: {
      createTitle: 'Sağlayıcı Ekle',
      editTitle: 'Sağlayıcıyı Düzenle',
      providerType: 'Sağlayıcı Türü',
      instanceName: 'Örnek Adı',
      instanceNameHint: 'Bu sağlayıcı yapılandırması için benzersiz bir ad.',
      baseUrl: 'Temel URL',
      defaultModel: 'Varsayılan Model',
      defaultContextWindow: 'Varsayılan Bağlam Penceresi',
      modelContextWindows: 'Model Başına Bağlam Geçersiz Kılmaları',
      modelContextWindowsHint: 'JSON nesnesi, ör. gpt-5.2: 200000',
      modelContextWindowsInvalid: 'Model başına bağlam geçersiz kılmaları geçerli JSON olmalıdır.',
      authType: 'Kimlik Doğrulama Türü',
      apiKey: 'API Anahtarı',
      apiKeyPlaceholder: 'Mevcut anahtarı korumak için boş bırakın',
      authFile: 'Kimlik Doğrulama Dosyası',
      oauthBrowserTitle: 'Tarayıcı Girişi (Yetki Kodu)',
      oauthRedirectUri: 'URI\'yi yönlendir',
      oauthStartBrowser: 'Tarayıcı Girişini Başlat',
      oauthAuthorizationUrl: 'Yetkilendirme URL\'si',
      oauthState: 'Eyalet',
      oauthCallbackUrl: 'Geri Arama URL\'sini Yapıştır',
      oauthCallbackHint: 'Kodu ve durumu içeren geri arama URL\'sinin tamamını yapıştırın.',
      oauthCompleteBrowser: 'Tarayıcı Girişini Tamamla',
      concurrency: 'Maksimum Eşzamanlılık',
      create: 'Oluştur',
      save: 'Kaydet',
      cancel: 'İptal',
      modelManualHint: 'Model kimliğini manuel olarak girin. Bu sağlayıcı türü için model listesi mevcut değil.',
      modelsLockedHint: 'Model seçiminin kilidini açmak için sağlayıcıyı bir API anahtarıyla kaydedin.',
      savedContinue: 'Sağlayıcı kaydedildi. Artık bir model seçebilirsiniz.',
      saveAndContinue: 'Kaydet ve Devam Et'
    },
    testSuccess: 'Sağlayıcı "{id}" ulaşılabilir durumda.',
    testFailed: 'Sağlayıcı "{id}" testi başarısız oldu',
    createSuccess: 'Sağlayıcı "{id}" oluşturuldu.',
    updateSuccess: 'Sağlayıcı "{id}" güncellendi.',
    deleteSuccess: 'Sağlayıcı "{id}" silindi.',
    defaultSuccess: 'Varsayılan sağlayıcı "{id}" olarak ayarlandı.',
    errors: {
      loadFailed: 'Sağlayıcılar yüklenemedi'
    }
  } as const;
