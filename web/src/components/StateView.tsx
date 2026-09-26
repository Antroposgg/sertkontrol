import type { ReactNode } from 'react';

import { problemMessage, type Problem } from '../api/problem';

/** Состояние экрана: каждый экран обязан показывать загрузку, пусто и ошибку с повтором (АРХ §8, КЕЙС §5.2 UX). */
export type ViewState<T> =
  | { kind: 'loading' }
  | { kind: 'empty' }
  | { kind: 'error'; problem: Problem }
  | { kind: 'ready'; data: T };

/** Свойства `StateView`. */
export interface StateViewProps<T> {
  state: ViewState<T>;
  /** Текст пустого состояния. */
  emptyText: string;
  /** Повтор после ошибки. */
  onRetry: () => void;
  /** Отрисовка данных. */
  children: (data: T) => ReactNode;
}

/** Единое отображение состояний «загрузка / пусто / ошибка + повторить / данные». */
export function StateView<T>({ state, emptyText, onRetry, children }: StateViewProps<T>) {
  switch (state.kind) {
    case 'loading':
      return (
        <p role="status" aria-busy="true">
          Загрузка…
        </p>
      );
    case 'empty':
      return <p>{emptyText}</p>;
    case 'error':
      return (
        <div role="alert">
          <p>{problemMessage(state.problem)}</p>
          <button type="button" onClick={onRetry}>
            Повторить
          </button>
        </div>
      );
    case 'ready':
      return <>{children(state.data)}</>;
  }
}
