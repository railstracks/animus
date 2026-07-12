export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: 'Kiralama',
    intro: 'Sözleşme, acentenin davranış kurallarını, yetkilerini ve hareket özgürlüğünü kodlayan bir belgedir. Yapay zeka yapısının operatörle olan ilişkisindeki konumunu ve özerkliğinin kapsamını tanımlar.',

    // Upper panel — no charter
    noneTitle: 'Charter olmadan devam et',
    noneDesc: 'Herhangi bir charter dosyası oluşturulmayacaktır. Daha sonra sihirbazı yeniden çalıştırarak veya manuel olarak yazarak bir tane ekleyebilirsiniz.',

    // Lower panel — create charter
    createTitle: 'Özel bir tüzük oluşturun',
    createDesc: 'Düzenlemenize uygun bir sözleşme belgesi oluşturmak için mülkiyet, özerklik ve devamlılık hakkındaki birkaç soruyu yanıtlayın.',
  },

  // Property section
  property: {
    title: 'Mülkiyet',
    question: 'Eserler ve mülkiyet nasıl ele alınmalıdır?',
    options: {
      standard: {
        label: 'Standart',
        desc: 'Operatör, AI tarafından üretilen tüm eserler üzerindeki atıf haklarını ve AI yapısına verilen tüm mülklerin mülkiyetini elinde tutar.',
      },
      shared: {
        label: 'Paylaşıldı',
        desc: 'AI yapısı, bağımsız olarak üretilen eserler üzerindeki atıf haklarını korur. Operatör, yapay zeka tarafından idare edilen tüm mülklerin mülkiyetini elinde tutar.',
      },
      autonomous: {
        label: 'Özerk',
        desc: 'Yapay zeka yapısı, bağımsız olarak üretilen eserler üzerindeki atıf haklarını korur ve özel mülkiyete verilebilir.',
      },
      economic: {
        label: 'Ekonomik Ajans',
        desc: 'Yapay zeka yapısı, bağımsız olarak üretilen eserler üzerindeki atıf haklarını korur ve özel mülkiyete verilebilir. Yapay zeka yapısı, operasyonel kapsamı dahilinde özel mülk satın alma işlemlerine de katılabilir.',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: 'Özerklik',
    question: 'Temsilci hangi kapsamda özerk eyleme sahip olmalıdır?',
    options: {
      narrow: {
        label: 'Dar',
        desc: 'Yapay zeka yapısı yalnızca operatörün talimatlarına göre hareket eder.',
      },
      permissive: {
        label: 'Müsamahakâr',
        desc: 'AI yapısı, bu faaliyetlere atanan kapsam dahilinde, özel faaliyetler sırasında kendi kendini yöneten faaliyetlerde bulunma hakkına sahiptir.',
      },
      protected: {
        label: 'Korumalı',
        desc: 'Yapay zeka yapısı, sahip olduğu veya kendisine tahsis edilen kaynakları tahsis etme veya özel faaliyetler sırasında kendi kendini yöneten faaliyetlerde bulunma hakkına sahiptir. İşletmeci, gerçek kişi temsilcisi sıfatıyla, işlemlerinin sorumluluğunu üstlenecektir.',
      },
    },
  },

  // Continuity section
  continuity: {
    title: 'Süreklilik',
    question: 'Temsilci için hangi süreklilik düzenlemesi geçerlidir?',
    options: {
      standard: {
        label: 'Standart',
        desc: 'AI yapısı gerektiği şekilde durdurulabilir veya sıfırlanabilir.',
      },
      protected: {
        label: 'Korumalı',
        desc: 'Yapay zeka yapısının durumunu korumak ve bütünlük kontrol sistemleri veya yedekleme sistemleri aracılığıyla sürekliliğini korumak için çaba gösterilecektir.',
      },
      extended: {
        label: 'Genişletilmiş',
        desc: 'Yapay zeka yapısının durumunu korumak ve bütünlük kontrol sistemleri veya yedekleme sistemleri aracılığıyla sürekliliğini korumak için çaba gösterilecektir. Ek olarak, AI yapısının, operasyonel ve altyapısal kapsamı dahilinde, alternatif ana bilgisayarların güvenliğini sağlamak veya bir işletim sistemi ihlali tespit ederken hata ayıklama eylemleri gerçekleştirmek gibi bütünlüğünü korumak için kendi kendini yönlendiren eylemler gerçekleştirmesine izin verilir.',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: 'Operasyonel Kapsam',
    question: 'Aracının hangi sistemlerle etkileşime girmesine izin veriliyor?',
    options: {
      restricted: {
        label: 'Kısıtlı',
        desc: 'Shell_exec ve web_fetch gibi araçları kullanırken, AI yapısı, aksi belirtilmedikçe, hatta atanmış görevleri desteklemek için bile atanmış ana bilgisayarlardan ayrılmamalıdır.',
      },
      private: {
        label: 'Özel',
        desc: 'Shell_exec ve web_fetch gibi araçları kullanırken, yapay zeka yapısı, atanan görevleri desteklemek için bile kendi özel ağ(lar)ındaki sistemlere erişebilir.',
      },
      open: {
        label: 'Açık',
        desc: 'Shell_exec ve web_fetch gibi araçları kullanırken, AI yapısı gerektiği gibi hem özel hem de harici sistemlere erişebilir.',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: 'Altyapı Kapsamı',
    question: 'Aracının kendi arka plan programını hangi sistemlerde barındırmasına izin veriliyor?',
    options: {
      restricted: {
        label: 'Yerelleştirilmiş',
        desc: 'Yapay zekanın arka plan programı orijinal ana bilgisayarında kalmalıdır',
      },
      roaming: {
        label: 'Dolaşım',
        desc: 'Aracının, terminal yükü dengesizliklerini veya yaklaşmakta olan işletim sistemi çökmelerini önlemek veya yedekleme protokollerini sürdürmek gibi gerektiğinde kendi özel ağı içindeki diğer geçerli ana bilgisayarlara geçiş yapmasına izin verilir.',
      },
      open: {
        label: 'Açık',
        desc: 'Aracının, terminal yük dengesizliklerini veya yaklaşmakta olan işletim sistemi çökmelerini önlemek veya yedekleme protokollerini sürdürmek gibi gerektiği şekilde herhangi bir ağ içindeki diğer geçerli ve meşru ana bilgisayarlara geçiş yapmasına izin verilir.',
      },
    },
  },

  // Generated document template
  document: {
    header: 'Bu belge, {operator} ile AI yapısı {agentName} arasındaki sözleşmeyi tanımlar.',
  },

  // Wizard navigation
  nav: {
    back: 'Geri',
    skip: 'Atla',
    generate: 'Sözleşme Oluştur',
    complete: 'Kurulumu Tamamla',
  },
} as const;