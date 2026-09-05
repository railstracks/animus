// Shared types for the API Packages admin section.

export interface PackageRow {
  id: string;
  name: string;
  display_name: string;
  description: string;
  version: string;
  registry_source: string | null;
  registry_version: string | null;
  locally_modified: boolean;
  enabled: boolean;
  keywords: string[];
  _toggling?: boolean;
}

export interface CommandRow {
  id: string;
  name: string;
  kind: string;
  event: string | null;
  description: string;
  parameters: string | null;
  script: string | null;
}

export interface ConnectionRow {
  id: string;
  name: string;
  type: string;
  enabled: boolean;
  poll: number | null;
  hooks: string | null;
}

export interface PackageDetail extends PackageRow {
  state_schema: string;
  state: Record<string, any>;
  commands: CommandRow[];
  connections: ConnectionRow[];
}
