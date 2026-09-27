import type { HistoryEntry, Level, Status } from './api/types';

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

/** Одна строка истории: «прекращён → действует с 26.09.2026», «появился в данных: действует». */
export function describeChange(entry: HistoryEntry): string {
  const { before, after } = entry;
  if (after === null) {
    return before === null ? 'исчез из данных' : `исчез из данных (был: ${before.status_name})`;
  }
  const since = after.status_date === null ? '' : ` с ${formatDate(after.status_date)}`;
  if (before === null) {
    return `появился в данных: ${after.status_name}${since}`;
  }
  if (before.status === after.status) {
    const until = after.expiry_date === null ? '' : ` (срок действия до ${formatDate(after.expiry_date)})`;
    return `${after.status_name}, изменились сроки${until}`;
  }
  return `${before.status_name} → ${after.status_name}${since}`;
}

/** Текст итога «Симулировать обновление»: сколько документов портфеля изменилось. */
export function simulateResultText(notified: number): string {
  if (notified === 0) {
    return 'Данные обновлены. Статусы документов в вашем портфеле не изменились — уведомления нет.';
  }
  return `Данные обновлены. Изменилось документов в портфеле: ${String(notified)} — уведомление придёт в чат с ботом.`;
}
