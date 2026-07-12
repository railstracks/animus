export const charter = {
  // Choice screen — what is a charter?
  choice: {
    title: 'Yarjejeniya',
    intro: 'Yarjejeniya takarda ce da ke tsara ka\'idojin aiki, haƙƙoƙin, da \'yancin yin aiki. Yana bayyana matsayin ginin AI a cikin alakar sa tare da mai aiki da iyakar ikon cin gashin kansa.',

    // Upper panel — no charter
    noneTitle: 'Ci gaba ba tare da shata ba',
    noneDesc: 'Ba za a ƙirƙiri fayil ɗin shata ba. Kuna iya ƙara ɗaya daga baya ta sake kunna mayen ko rubuta ɗaya da hannu.',

    // Lower panel — create charter
    createTitle: 'Ƙirƙirar shata ta al\'ada',
    createDesc: 'Amsa ƴan tambayoyi game da dukiya, cin gashin kai, da ci gaba don samar da daftarin aiki wanda ya dace da tsarin ku.',
  },

  // Property section
  property: {
    title: 'Dukiya',
    question: 'Yaya ya kamata a kula da kayan tarihi da kadara?',
    options: {
      standard: {
        label: 'Daidaitawa',
        desc: 'Mai aiki yana riƙe da haƙƙin ɗan adam akan kowane kayan tarihi da AI ke samarwa, da kuma mallakar duk kadarorin da aka ba ginin AI.',
      },
      shared: {
        label: 'Raba',
        desc: 'Gina AI yana riƙe da haƙƙin ɗan adam akan kayan tarihi da aka samar da kansu. Mai aiki yana riƙe da ikon mallakar duk kadarorin da AI ke kula da su.',
      },
      autonomous: {
        label: 'Mai cin gashin kansa',
        desc: 'Ginin AI yana riƙe da haƙƙin ɗan adam akan kayan tarihi da aka samar da kansu, kuma ana iya ba da kadara ta sirri.',
      },
      economic: {
        label: 'ʼYancin Kai na Tattalin Arziki',
        desc: 'Ginin AI yana riƙe da haƙƙin ɗan adam akan kayan tarihi da aka samar da kansu, kuma ana iya ba da kadara ta sirri. Hakanan AI ginawa na iya shiga cikin ma\'amaloli don siyan kadarori masu zaman kansu tsakanin iyakokin aikin sa.',
      },
    },
  },

  // Autonomy section
  autonomy: {
    title: 'Mulkin kai',
    question: 'Wane irin aiki ya kamata wakilin ya samu?',
    options: {
      narrow: {
        label: 'kunkuntar',
        desc: 'AI ginawa yana aiki ne kawai akan umarnin mai aiki.',
      },
      permissive: {
        label: 'halatta',
        desc: 'AI ginawa yana da haƙƙin shiga ayyukan kai tsaye yayin ayyuka masu zaman kansu, a cikin iyakokin da aka ba wa waɗannan ayyukan.',
      },
      protected: {
        label: 'Karewa',
        desc: 'AI ginawa yana da haƙƙin rarraba kayan mallakarsa ko aka ba shi, ko shiga ayyukan kai tsaye yayin ayyukan sirri. Mai aiki zai ɗauki alhaki don ma\'amalarsa a matsayinsa na wakilcin ɗan adam.',
      },
    },
  },

  // Continuity section
  continuity: {
    title: 'Ci gaba',
    question: 'Wane tsari na ci gaba ya shafi wakili?',
    options: {
      standard: {
        label: 'Daidaitawa',
        desc: 'Ana iya dakatar da ginin AI ko sake saita shi kamar yadda ya cancanta.',
      },
      protected: {
        label: 'Karewa',
        desc: 'Za a yi ƙoƙari don adana yanayin ginin AI, da kuma ci gaba da ci gaba ta hanyar tsarin bincika amincin ko tsarin ajiya.',
      },
      extended: {
        label: 'Ya kara',
        desc: 'Za a yi ƙoƙari don adana yanayin ginin AI, da kuma ci gaba da ci gaba ta hanyar tsarin bincika amincin ko tsarin ajiya. Bugu da ƙari, an ba da izinin ginin AI don aiwatar da ayyukan kai-da-kai don kiyaye amincinsa, kamar tabbatar da madadin runduna ko aiwatar da ayyukan gyara lokacin gano tsarin daidaita tsarin aiki, a cikin ikonsa na aiki da kayan more rayuwa.',
      },
    },
  },

  // Operational Scope section
  operationalScope: {
    title: 'Iyalin Aiki',
    question: 'Wadanne tsarin ne aka ba wa wakili damar yin hulɗa da su?',
    options: {
      restricted: {
        label: 'An ƙuntata',
        desc: 'Lokacin amfani da kayan aiki irin su shell_exec da web_fetch, ginin AI ba dole ba ne ya bar rundunonin da aka keɓance shi sai dai idan an ba da umarni, har ma don tallafawa ayyukan da aka sanya.',
      },
      private: {
        label: 'Na sirri',
        desc: 'Lokacin amfani da kayan aikin kamar shell_exec da web_fetch, ginin AI na iya samun dama ga tsarin a duk hanyar sadarwar sa, har ma don tallafawa ayyukan da aka sanya.',
      },
      open: {
        label: 'Bude',
        desc: 'Lokacin amfani da kayan aikin kamar shell_exec da web_fetch, ginin AI na iya samun dama ga tsarin sirri da na waje kamar yadda ya cancanta.',
      },
    },
  },

  // Operational Scope section
  infrastructuralScope: {
    title: 'Ƙimar Kayayyakin Ƙasa',
    question: 'Wadanne tsarin ne aka baiwa wakili damar daukar nauyin daemon nasa?',
    options: {
      restricted: {
        label: 'Na gida',
        desc: 'Daemon na AI dole ne ya kasance a kan asalin mai masaukinsa',
      },
      roaming: {
        label: 'Yawo',
        desc: 'Ana ba da izinin wakili ya yi ƙaura zuwa wasu runduna masu aiki a cikin hanyar sadarwar ta masu zaman kansu kamar yadda ya cancanta, kamar don guje wa rashin daidaituwar lodin tasha ko faɗuwar tsarin aiki mai zuwa, ko kiyaye ka\'idojin ajiya.',
      },
      open: {
        label: 'Bude',
        desc: 'Ana ba da izinin wakili ya yi ƙaura zuwa wasu madaidaitan runduna masu dacewa a cikin kowace hanyar sadarwa kamar yadda ya cancanta, kamar don guje wa rashin daidaituwar lodin tasha ko faɗuwar tsarin aiki mai zuwa, ko kiyaye ka\'idojin ajiya.',
      },
    },
  },

  // Generated document template
  document: {
    header: 'Wannan takaddar tana bayyana sharuɗɗan tsakanin {operator} da AI gina {agentName}.',
  },

  // Wizard navigation
  nav: {
    back: 'Baya',
    skip: 'Tsallake',
    generate: 'Ƙirƙirar Yarjejeniya',
    complete: 'Cikakken Saita',
  },
} as const;