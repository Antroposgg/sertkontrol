import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';

import { StateView } from './StateView';

describe('StateView', () => {
  const noop = () => undefined;

  it('загрузка', () => {
    render(<StateView state={{ kind: 'loading' }} emptyText="пусто" onRetry={noop}>{() => null}</StateView>);
    expect(screen.getByRole('status')).toHaveTextContent('Загрузка');
  });

  it('пусто', () => {
    render(<StateView state={{ kind: 'empty' }} emptyText="Портфель пуст" onRetry={noop}>{() => null}</StateView>);
    expect(screen.getByText('Портфель пуст')).toBeInTheDocument();
  });

  it('ошибка с повтором', async () => {
    const onRetry = vi.fn();
    render(
      <StateView state={{ kind: 'error', problem: { type: 'x', title: 't', status: 0, code: 'network' } }} emptyText="" onRetry={onRetry}>
        {() => null}
      </StateView>,
    );
    expect(screen.getByRole('alert')).toHaveTextContent('Нет связи');
    await userEvent.click(screen.getByRole('button', { name: 'Повторить' }));
    expect(onRetry).toHaveBeenCalledOnce();
  });

  it('данные', () => {
    render(<StateView state={{ kind: 'ready', data: 3 }} emptyText="" onRetry={noop}>{(n) => <span>записей: {n}</span>}</StateView>);
    expect(screen.getByText('записей: 3')).toBeInTheDocument();
  });
});
