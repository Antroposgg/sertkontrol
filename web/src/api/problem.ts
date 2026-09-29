/**
 * Ошибки API в формате RFC 9457 (`application/problem+json`), АРХ §8.
 */

/** Машинные коды ошибок сервера (`sk::ErrorCode`) и клиентский `network`. */
export type ProblemCode =
  | 'invalid_argument'
  | 'unauthorized'
  | 'init_data_expired'
  | 'forbidden'
  | 'not_found'
  | 'conflict'
  | 'file_too_large'
  | 'unsupported_media_type'
  | 'number_not_recognized'
  | 'not_found_in_snapshot'
  | 'snapshot_unavailable'
  | 'rate_limited'
  | 'consent_required'
  | 'internal'
  | 'network';

/** Тело ответа об ошибке. */
export interface Problem {
  type: string;
  title: string;
  status: number;
  detail?: string;
  code?: ProblemCode;
}

/** Ошибка вызова API с разобранным телом RFC 9457. */
export class ApiError extends Error {
  readonly problem: Problem;

  constructor(problem: Problem) {
    super(problem.detail ?? problem.title);
    this.name = 'ApiError';
    this.problem = problem;
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}

/**
 * Приводит произвольное тело ответа к `Problem`. Неполное или не-JSON тело
 * дополняется из HTTP-статуса, чтобы UI всегда мог показать ошибку.
 */
export function toProblem(body: unknown, status: number, statusText: string): Problem {
  const problem: Problem = { type: 'about:blank', title: statusText || `HTTP ${String(status)}`, status };
  if (!isRecord(body)) {
    return problem;
  }
  if (typeof body['type'] === 'string') problem.type = body['type'];
  if (typeof body['title'] === 'string') problem.title = body['title'];
  if (typeof body['detail'] === 'string') problem.detail = body['detail'];
  if (typeof body['code'] === 'string') problem.code = body['code'] as ProblemCode;
  return problem;
}

/** Человекочитаемое сообщение для экрана ошибки. */
export function problemMessage(problem: Problem): string {
  switch (problem.code) {
    case 'init_data_expired':
    case 'unauthorized':
      return 'Сессия устарела. Закройте и снова откройте приложение из чата с ботом.';
    case 'network':
      return 'Нет связи с сервером. Проверьте подключение и повторите.';
    case 'rate_limited':
      return 'Слишком много запросов. Попробуйте через минуту.';
    case 'consent_required':
      return 'Нужно согласие на обработку данных — оно запрашивается при открытии приложения.';
    default:
      return problem.detail ?? problem.title;
  }
}
