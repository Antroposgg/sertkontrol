/**
 * Типы ответов REST `/api/v1` — зеркало схем `openapi.yaml` (контракт C7).
 * Меняются только вместе с `openapi.yaml` и `docs/changelog-contracts.md`.
 */

/** Статус документа в реестре. */
export type Status = 'unknown' | 'active' | 'suspended' | 'terminated' | 'annulled' | 'archived';

/** Уровень вердикта. */
export type Level = 'ok' | 'warning' | 'problem' | 'not_found' | 'needs_confirmation';

/** Основание строки карточки (КЕЙС §7 п.3). */
export type Basis = 'fact' | 'calculation' | 'recommendation';

export interface Finding {
  basis: Basis;
  rule: string;
  text: string;
}

export interface Card {
  kind: 'declaration' | 'certificate';
  status: Status;
  status_name: string;
  issue_date: string | null;
  expiry_date: string | null;
  status_date: string | null;
  applicant_name: string;
  applicant_inn: string;
  manufacturer_name: string;
  product: string;
  tnved: string;
  registry_url: string;
}

export interface Suggestion {
  number: string;
  display_number: string;
  distance: number;
}

export interface Verdict {
  query: string;
  number: string | null;
  display_number: string | null;
  level: Level;
  data_date: string;
  snapshot_version: number;
  is_demo: boolean;
  distance: number;
  card: Card | null;
  findings: Finding[];
  suggestions: Suggestion[];
}

export interface CheckedVerdict extends Verdict {
  check_id: number;
}

export interface PortfolioItem {
  id: number;
  doc_key: string;
  display_number: string;
  doc_kind: 'declaration' | 'certificate';
  sku: string | null;
  supplier_inn: string | null;
  last_status: Status;
  last_version: number;
}

export interface PortfolioPage {
  items: PortfolioItem[];
  next_cursor: string | null;
}

export interface AddResult {
  item: PortfolioItem;
  verdict: Verdict;
}

export interface Me {
  max_user_id: number;
  portfolio_count: number;
  is_demo: boolean;
  demo_stage: 'base' | 'updated';
  consented: boolean;
}

export interface DataStatus {
  version: number;
  source: string;
  source_date: string;
  record_count: number;
  is_demo: boolean;
  next_update: string | null;
}

/** Фильтр портфеля. */
export interface PortfolioFilter {
  status?: Status;
  supplierInn?: string;
  cursor?: string;
}

/** Запрос «поставить на контроль». */
export interface AddRequest {
  number: string;
  sku?: string;
  supplier_inn?: string;
}
