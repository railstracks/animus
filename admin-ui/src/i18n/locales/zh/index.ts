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
import { constitution } from './constitution'
import { logs } from './logs'
import { agents } from './agents'
import { activeMemory } from './activeMemory'
import { channels } from './channels'
import { charter } from './charter'
import { templates } from './templates'

export const zh = {
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
  constitution,
  logs,
  agents,
  activeMemory,
  channels,
  charter,
  templates,
} as const;
