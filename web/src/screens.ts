/** Экраны мини-приложения (АРХ §8). Содержимое появляется на этапах 1–2 (docs/plan.md). */
export const SCREENS = [
  { id: 'portfolio', title: 'Портфель' },
  { id: 'add', title: 'Добавить' },
  { id: 'data', title: 'Данные' },
] as const;

/** Идентификатор экрана. */
export type ScreenId = (typeof SCREENS)[number]['id'];
