import type { Level, Status } from './api/types';

/** `YYYY-MM-DD` → `ДД.ММ.ГГГГ`. */
export function formatDate(iso: string): string {
  const [y, m, d] = iso.split('-');
  return y !== undefined && m !== undefined && d !== undefined ? `${d}.${m}.${y}` : iso;
}

/** Названия статусов для фильтра и списка. */
export const STATUS_NAMES: Record<Status, string> = {
  unknown: 'не распознан',
  active: 'действует',
  suspended: 'приостановлен',
  terminated: 'прекращён',
  annulled: 'аннулирован',
  archived: 'архивный',
};

/** Значок уровня вердикта — тот же, что в боте. */
export const LEVEL_ICONS: Record<Level, string> = {
  ok: '✅',
  warning: '⚠️',
  problem: '⛔',
  not_found: '❓',
  needs_confirmation: '❓',
};
