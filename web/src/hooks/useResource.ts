import { useCallback, useEffect, useMemo, useState } from 'react';

import { ApiError, type Problem } from '../api/problem';
import type { ViewState } from '../components/StateView';

/** Приводит любую ошибку к `Problem` для экрана ошибки. */
export function toProblemOf(error: unknown): Problem {
  if (error instanceof ApiError) {
    return error.problem;
  }
  return { type: 'about:blank', title: error instanceof Error ? error.message : 'Ошибка', status: 0 };
}

/**
 * Загружает данные и отдаёт состояние экрана «загрузка / пусто / ошибка / данные» (АРХ §8).
 * @param load функция загрузки; меняйте её через `useCallback`, иначе загрузка повторится на каждом рендере.
 * @param isEmpty когда данные считаются пустыми.
 * @returns состояние и `reload` для кнопки «Повторить» и обновления после действий.
 */
export function useResource<T>(load: () => Promise<T>, isEmpty: (data: T) => boolean = () => false) {
  const [attempt, setAttempt] = useState(0);
  const reload = useCallback(() => {
    setAttempt((n) => n + 1);
  }, []);
  // Запрос — пара (load, attempt). Результат помнит, к какому запросу относится: пока ответа на текущий
  // запрос нет, экран показывает «загрузку» без синхронного setState в эффекте.
  const request = useMemo(() => ({ load, attempt }), [load, attempt]);
  const [result, setResult] = useState<{ request: typeof request; state: ViewState<T> } | null>(null);

  useEffect(() => {
    let cancelled = false;
    request.load().then(
      (data) => {
        if (!cancelled) setResult({ request, state: isEmpty(data) ? { kind: 'empty' } : { kind: 'ready', data } });
      },
      (error: unknown) => {
        if (!cancelled) setResult({ request, state: { kind: 'error', problem: toProblemOf(error) } });
      },
    );
    return () => {
      cancelled = true;
    };
    // isEmpty — чистый предикат; перезагрузка определяется запросом.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [request]);

  const state: ViewState<T> = result?.request === request ? result.state : { kind: 'loading' };
  return { state, reload };
}
