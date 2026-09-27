import { createContext, useContext } from 'react';

import type { SertkontrolApi } from './sertkontrol';

/** API мини-приложения; в тестах подменяется фейком. */
export const ApiContext = createContext<SertkontrolApi | null>(null);

/** API из контекста. Без провайдера — ошибка программиста. */
export function useApi(): SertkontrolApi {
  const api = useContext(ApiContext);
  if (api === null) {
    throw new Error('ApiContext.Provider не задан');
  }
  return api;
}
