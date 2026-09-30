/**
 * Клиент REST `/api/v1` (контракт C7, `openapi.yaml` — этап 1).
 *
 * Каждый запрос несёт сырую initData в `X-Max-Init-Data` (ADR-0006): сервер проверяет
 * HMAC на каждый запрос, cookie и сессий нет.
 */
import { ApiError, toProblem } from './problem';

/** Параметры клиента. */
export interface ApiClientOptions {
  /** Префикс API, по умолчанию `/api/v1` (тот же origin, что и мини-приложение). */
  baseUrl?: string;
  /** Источник initData; обычно `getInitData` из `max/bridge`. */
  getInitData: () => string | undefined;
  /** Реализация fetch (инъекция для тестов). */
  fetchImpl?: typeof fetch;
}

/** Методы клиента. Ошибки — всегда `ApiError`. */
export interface ApiClient {
  get<T>(path: string): Promise<T>;
  post<T>(path: string, body: unknown): Promise<T>;
  /** POST `multipart/form-data` (граница выставляет браузер). */
  postForm<T>(path: string, form: FormData): Promise<T>;
  /** POST текста с заданным типом (например, `text/csv` для импорта, F9); ответ — JSON. */
  postText<T>(path: string, text: string, contentType: string): Promise<T>;
  /** POST без тела запроса и ответа (`204`). */
  postEmpty(path: string): Promise<void>;
  del(path: string): Promise<void>;
}

/** Создаёт клиент API. */
export function createApiClient(options: ApiClientOptions): ApiClient {
  const baseUrl = options.baseUrl ?? '/api/v1';
  const doFetch = options.fetchImpl ?? ((input, init) => fetch(input, init));

  async function request(method: string, path: string, body?: unknown, contentType?: string): Promise<Response> {
    const headers = new Headers({ Accept: 'application/json' });
    const initData = options.getInitData();
    if (initData !== undefined) {
      headers.set('X-Max-Init-Data', initData);
    }
    const init: RequestInit = { method, headers };
    if (body instanceof FormData) {
      init.body = body;
    } else if (typeof body === 'string' && contentType !== undefined) {
      headers.set('Content-Type', contentType);
      init.body = body;
    } else if (body !== undefined) {
      headers.set('Content-Type', 'application/json');
      init.body = JSON.stringify(body);
    }
    let response: Response;
    try {
      response = await doFetch(`${baseUrl}${path}`, init);
    } catch (cause) {
      throw new ApiError({
        type: 'about:blank',
        title: 'Сеть недоступна',
        status: 0,
        code: 'network',
        detail: cause instanceof Error ? cause.message : String(cause),
      });
    }
    if (!response.ok) {
      let parsed: unknown = undefined;
      try {
        parsed = await response.json();
      } catch {
        // Тело не JSON — Problem будет собран из статуса.
      }
      throw new ApiError(toProblem(parsed, response.status, response.statusText));
    }
    return response;
  }

  return {
    async get<T>(path: string): Promise<T> {
      return (await (await request('GET', path)).json()) as T;
    },
    async post<T>(path: string, body: unknown): Promise<T> {
      return (await (await request('POST', path, body)).json()) as T;
    },
    async postForm<T>(path: string, form: FormData): Promise<T> {
      return (await (await request('POST', path, form)).json()) as T;
    },
    async postText<T>(path: string, text: string, contentType: string): Promise<T> {
      return (await (await request('POST', path, text, contentType)).json()) as T;
    },
    async postEmpty(path: string): Promise<void> {
      await request('POST', path);
    },
    async del(path: string): Promise<void> {
      await request('DELETE', path);
    },
  };
}
