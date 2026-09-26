/**
 * Доступ к MAX Bridge (`window.WebApp`).
 *
 * Скрипт MAX Bridge подключается на этапе 1 после сверки с актуальной документацией MAX.
 * До этого (и вне клиента MAX, например в браузере разработчика) объекта нет,
 * и функции возвращают `undefined` вместо исключения.
 */

/** Подмножество MAX Bridge, которое использует мини-приложение. */
export interface MaxWebApp {
  /** Сырая строка initData: передаётся на сервер как есть в `X-Max-Init-Data` (АРХ §8). */
  initData?: string;
  /** Платформа запуска: `ios`, `android`, `desktop`, `web`. */
  platform?: string;
}

declare global {
  interface Window {
    WebApp?: MaxWebApp;
  }
}

/**
 * Возвращает объект MAX Bridge, если мини-приложение открыто внутри MAX.
 * @param win окно (инъекция для тестов).
 */
export function getWebApp(win: Window = window): MaxWebApp | undefined {
  return win.WebApp;
}

/**
 * Сырая строка initData или `undefined`, если приложение открыто вне MAX.
 * @param win окно (инъекция для тестов).
 */
export function getInitData(win: Window = window): string | undefined {
  const raw = getWebApp(win)?.initData;
  return raw === undefined || raw === '' ? undefined : raw;
}
