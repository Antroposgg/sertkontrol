import { screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { fakeApi } from '../test/fakeApi';
import { renderWithApi } from '../test/render';
import { Data } from './Data';

describe('Данные', () => {
  it('показывает дату данных и пометку тестовых данных', async () => {
    renderWithApi(<Data />, fakeApi());
    expect(await screen.findByText('25.09.2026')).toBeInTheDocument();
    expect(screen.getByText(/Тестовые данные/)).toBeInTheDocument();
  });

  it('ошибка', async () => {
    renderWithApi(<Data />, fakeApi({ dataStatus: vi.fn(() => Promise.reject(new Error('сбой'))) }));
    expect(await screen.findByRole('alert')).toHaveTextContent('сбой');
  });
});
