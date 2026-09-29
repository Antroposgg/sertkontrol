/** Экраны мини-приложения (АРХ §8). */
export const SCREENS = [
  { id: 'portfolio', title: 'Портфель' },
  { id: 'document', title: 'Документ' },
  { id: 'add', title: 'Добавить' },
  { id: 'import', title: 'Импорт' },
  { id: 'data', title: 'Данные' },
] as const;

/** Идентификатор экрана. */
export type ScreenId = (typeof SCREENS)[number]['id'];
