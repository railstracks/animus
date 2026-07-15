import { app } from './app'
import { sidebar } from './sidebar'
import { common } from './common'
import { dashboard } from './dashboard'
import { chat } from './chat'
import { memory } from './memory'
import { memorySearch } from './memorySearch'
import { memoryFiles } from './memoryFiles'
import { ontology } from './ontology'
import { providers } from './providers'
import { config } from './config'
import { logs } from './logs'
import { agents } from './agents'
import { activeMemory } from './activeMemory'
import { channels } from './channels'
import { charter } from './charter'
import { webSearch } from './webSearch'
import { templates } from './templates'

export const fr = {
  app,
  sidebar,
  common,
  dashboard,
  chat,
  memory,
  memorySearch,
  memoryFiles,
  ontology,
  providers,
  config,
  logs,
  agents,
  activeMemory,
  channels,
  charter,
  webSearch,
  templates,
} as const;
