/** Экраны мини-приложения (АРХ §8). «Импорт CSV» — этап 4 (F9). */
export const SCREENS = [
  { id: 'portfolio', title: 'Портфель' },
  { id: 'document', title: 'Документ' },
  { id: 'add', title: 'Добавить' },
  { id: 'data', title: 'Данные' },
] as const;

/** Идентификатор экрана. */
export type ScreenId = (typeof SCREENS)[number]['id'];
