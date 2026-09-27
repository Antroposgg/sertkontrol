/**
 * Типизированные вызовы REST `/api/v1` (C7) поверх `ApiClient`.
 */
import type { ApiClient } from './client';
import type {
  AddRequest,
  AddResult,
  CheckedVerdict,
  DataStatus,
  DemoUpdate,
  DocumentHistory,
  Me,
  PortfolioFilter,
  PortfolioPage,
} from './types';

/** Методы API мини-приложения. */
export interface SertkontrolApi {
  me: () => Promise<Me>;
  check: (number: string) => Promise<CheckedVerdict>;
  checkFile: (file: File) => Promise<CheckedVerdict[]>;
  listPortfolio: (filter: PortfolioFilter) => Promise<PortfolioPage>;
  addToPortfolio: (request: AddRequest) => Promise<AddResult>;
  removeFromPortfolio: (id: number) => Promise<void>;
  dataStatus: () => Promise<DataStatus>;
  /** История документа по номеру в любом написании (`GET /history?number=`, ADR-0014). */
  history: (number: string) => Promise<DocumentHistory>;
  /** Демо: применить снапшот N+1 и разослать уведомления (F6). */
  simulateUpdate: () => Promise<DemoUpdate>;
  /** Демо: вернуться к снапшоту N, чтобы пройти сценарий заново. */
  resetDemo: () => Promise<void>;
}

/** Создаёт API поверх клиента. */
export function createSertkontrolApi(client: ApiClient): SertkontrolApi {
  return {
    me: () => client.get<Me>('/me'),
    check: (number) => client.get<CheckedVerdict>(`/check?${new URLSearchParams({ number }).toString()}`),
    checkFile: (file) => {
      const form = new FormData();
      form.append('file', file);
      return client.postForm<CheckedVerdict[]>('/check/file', form);
    },
    listPortfolio: (filter) => {
      const params = new URLSearchParams();
      if (filter.status !== undefined) params.set('status', filter.status);
      if (filter.supplierInn !== undefined && filter.supplierInn !== '') params.set('supplier_inn', filter.supplierInn);
      if (filter.cursor !== undefined) params.set('cursor', filter.cursor);
      const query = params.toString();
      return client.get<PortfolioPage>(query === '' ? '/portfolio' : `/portfolio?${query}`);
    },
    addToPortfolio: (request) => client.post<AddResult>('/portfolio', request),
    removeFromPortfolio: (id) => client.del(`/portfolio/${String(id)}`),
    dataStatus: () => client.get<DataStatus>('/data-status'),
    history: (number) => client.get<DocumentHistory>(`/history?${new URLSearchParams({ number }).toString()}`),
    simulateUpdate: () => client.post<DemoUpdate>('/demo/simulate-update', undefined),
    resetDemo: () => client.postEmpty('/demo/reset'),
  };
}
