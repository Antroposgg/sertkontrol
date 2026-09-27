import { screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';

import { ApiError } from '../api/problem';
import type { DataStatus } from '../api/types';
import { dataStatus, fakeApi } from '../test/fakeApi';
import { renderWithApi } from '../test/render';
import { Data } from './Data';

const updated: DataStatus = { ...dataStatus, version: 2, source_date: '2026-09-26', demo_stage: 'updated' };

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

  it('боевые данные — без демо-сценария', async () => {
    renderWithApi(<Data />, fakeApi({ dataStatus: vi.fn(() => Promise.resolve({ ...dataStatus, is_demo: false, source: 'fsa' })) }));
    expect(await screen.findByText('fsa')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Симулировать обновление' })).not.toBeInTheDocument();
  });

  it('симулировать обновление → итог и данные N+1 → сбросить демо', async () => {
    const status = vi.fn().mockResolvedValueOnce(dataStatus).mockResolvedValueOnce(updated).mockResolvedValue(dataStatus);
    const api = fakeApi({ dataStatus: status });
    renderWithApi(<Data />, api);
    await userEvent.click(await screen.findByRole('button', { name: 'Симулировать обновление' }));
    expect(await screen.findByText(/Изменилось документов в портфеле: 1/)).toBeInTheDocument();
    expect(await screen.findByText('26.09.2026')).toBeInTheDocument();
    expect(api.simulateUpdate).toHaveBeenCalledTimes(1);
    await userEvent.click(screen.getByRole('button', { name: 'Сбросить демо' }));
    expect(await screen.findByText(/Демо сброшено/)).toBeInTheDocument();
    expect(await screen.findByText('25.09.2026')).toBeInTheDocument();
    expect(api.resetDemo).toHaveBeenCalledTimes(1);
  });

  it('N+1 ещё не загружен — кнопка недоступна', async () => {
    renderWithApi(<Data />, fakeApi({ dataStatus: vi.fn(() => Promise.resolve({ ...dataStatus, demo_update_available: false })) }));
    expect(await screen.findByRole('button', { name: 'Симулировать обновление' })).toBeDisabled();
    expect(screen.getByText(/ещё загружается/)).toBeInTheDocument();
  });

  it('ошибка демо-действия', async () => {
    const problem = { type: 'x', title: 'Нет', status: 403, code: 'forbidden' as const, detail: 'демо-сценарий доступен только в демо-режиме' };
    const api = fakeApi({ simulateUpdate: vi.fn(() => Promise.reject(new ApiError(problem))) });
    renderWithApi(<Data />, api);
    await userEvent.click(await screen.findByRole('button', { name: 'Симулировать обновление' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('только в демо-режиме');
    await waitFor(() => {
      expect(api.dataStatus).toHaveBeenCalledTimes(1);
    });
  });
});
