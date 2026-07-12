import { createI18n } from 'vue-i18n';

import { ar } from './locales/ar';
import { bn } from './locales/bn';
import { de } from './locales/de';
import { en } from './locales/en';
import { es } from './locales/es';
import { fa } from './locales/fa';
import { fr } from './locales/fr';
import { ha } from './locales/ha';
import { he } from './locales/he';
import { hi } from './locales/hi';
import { ja } from './locales/ja';
import { km } from './locales/km';
import { koKP } from './locales/ko-KP';
import { koKR } from './locales/ko-KR';
import { nl } from './locales/nl';
import { pl } from './locales/pl';
import { pt } from './locales/pt';
import { ru } from './locales/ru';
import { tr } from './locales/tr';
import { uk } from './locales/uk';
import { vi } from './locales/vi';
import { yue } from './locales/yue';
import { zh } from './locales/zh';

const LOCALE_STORAGE_KEY = 'animus_admin_locale';

export type LocaleType = 'ar' | 'bn' | 'de' | 'en' | 'es' | 'fa' | 'fr' | 'ha' | 'he' | 'hi' | 'ja' | 'km' | 'koKP' | 'koKR' | 'nl' | 'pl' | 'pt' | 'ru' | 'tr' | 'uk' | 'vi' | 'yue' | 'zh';
export type LocaleSelectItem = { label: string; value: LocaleType };

const messages = {
  ar,
  bn,
  de,
  en,
  es,
  fa,
  fr,
  ha,
  he,
  hi,
  ja,
  km,
  koKP,
  koKR,
  nl,
  pl,
  pt,
  ru,
  tr,
  uk,
  vi,
  yue,
  zh
} as const;

export type SupportedLocale = keyof typeof messages;
export const LocaleSelectItems: LocaleSelectItem[] = [
  {
    label: 'العربية',
    value: 'ar'
  },
  {
    label: 'বাংলা',
    value: 'bn'
  },
  {
    label: 'Deutsch',
    value: 'de'
  },
  {
    label: 'English',
    value: 'en'
  },
  {
    label: 'Español',
    value: 'es'
  },
  {
    label: 'فارسی',
    value: 'fa'
  },
  {
    label: 'Français',
    value: 'fr'
  },
  {
    label: 'Hausa',
    value: 'ha'
  },
  {
    label: 'עִבְרִית',
    value: 'he'
  },
  {
    label: 'हिन्दी',
    value: 'hi'
  },
  {
    label: '日本語',
    value: 'ja'
  },
  {
    label: 'ភាសាខ្មែរ',
    value: 'km'
  },
  {
    label: '문화어',
    value: 'koKP'
  },
  {
    label: '표준어',
    value: 'koKR'
  },
  {
    label: 'Nederlands',
    value: 'nl'
  },
  {
    label: 'Polski',
    value: 'pl'
  },
  {
    label: 'Português',
    value: 'pt'
  },
  {
    label: 'Русский',
    value: 'ru'
  },
  {
    label: 'Türkçe',
    value: 'tr'
  },
  {
    label: 'Українська',
    value: 'uk'
  },
  {
    label: 'Tiếng Việt',
    value: 'vi'
  },
  {
    label: '粵語',
    value: 'yue'
  },
  {
    label: '中文',
    value: 'zh'
  }
];

const localeValueSet: ReadonlySet<LocaleType> = new Set(
  LocaleSelectItems.map((item) => item.value)
);

export function isLocaleType(value: unknown): value is LocaleType {
  return typeof value === 'string' && localeValueSet.has(value as LocaleType);
}

function resolveInitialLocale(): SupportedLocale {
  const stored = typeof localStorage !== 'undefined' ? localStorage.getItem(LOCALE_STORAGE_KEY) : null;
  if (isLocaleType(stored)) {
    return stored;
  }
  return 'en';
}

function applyDocumentLang(locale: SupportedLocale): void {
  if (typeof document !== 'undefined' && document.documentElement) {
    document.documentElement.lang = locale;
  }
}

export const i18n = createI18n({
  legacy: false,
  locale: resolveInitialLocale(),
  fallbackLocale: 'en',
  messages
});

applyDocumentLang(i18n.global.locale.value as SupportedLocale);

export function setLocale(locale: SupportedLocale): void {
  i18n.global.locale.value = locale;
  if (typeof localStorage !== 'undefined') {
    localStorage.setItem(LOCALE_STORAGE_KEY, locale);
  }
  applyDocumentLang(locale);
}
