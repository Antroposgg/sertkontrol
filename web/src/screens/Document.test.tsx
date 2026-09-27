import { screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';

import { ApiError } from '../api/problem';
import { fakeApi, history } from '../test/fakeApi';
import { renderWithApi } from '../test/render';
import { Document } from './Document';

const conflict = { type: 'x', title: 'Уже на контроле', status: 409, code: 'conflict' as const };

describe('Документ', () => {
  it('без номера — подсказка', () => {
    const api = fakeApi();
    renderWithApi(<Document />, api);
    return waitFor(() => {
      expect(screen.getByText(/Введите номер документа/)).toBeInTheDocument();
      expect(api.check).not.toHaveBeenCalled();
    });
  });

  it('номер из портфеля → карточка и история', async () => {
    const api = fakeApi();
    renderWithApi(<Document number="RU Д-CR.PA08.B.89369/26" />, api);
    expect(screen.getByRole('status')).toHaveTextContent('Загрузка');
    expect(await screen.findByRole('article', { name: /Вердикт/ })).toBeInTheDocument();
    expect(screen.getByText(/действует → приостановлен с 26\.09\.2026/)).toBeInTheDocument();
    expect(api.history).toHaveBeenCalledWith('RU Д-CR.PA08.B.89369/26');
  });

  it('ввод номера и пустая история', async () => {
    const api = fakeApi({ history: vi.fn(() => Promise.resolve({ ...history, entries: [] })) });
    renderWithApi(<Document />, api);
    await userEvent.type(screen.getByLabelText('Номер документа'), '  RU Д-1/26 ');
    await userEvent.click(screen.getByRole('button', { name: 'Показать' }));
    expect(await screen.findByText(/статус не менялся/)).toBeInTheDocument();
    expect(api.check).toHaveBeenCalledWith('RU Д-1/26');
  });

  it('ошибка и повтор', async () => {
    const hist = vi.fn().mockRejectedValueOnce(new Error('сбой')).mockResolvedValue(history);
    renderWithApi(<Document number="RU Д-1/26" />, fakeApi({ history: hist }));
    expect(await screen.findByRole('alert')).toHaveTextContent('сбой');
    await userEvent.click(screen.getByRole('button', { name: 'Повторить' }));
    expect(await screen.findByText(/действует → приостановлен/)).toBeInTheDocument();
  });

  it('на контроль: успех и ошибка', async () => {
    const add = vi.fn().mockRejectedValueOnce(new ApiError(conflict)).mockResolvedValue({});
    const api = fakeApi({ addToPortfolio: add });
    renderWithApi(<Document number="RU Д-CR.PA08.B.89369/26" />, api);
    await userEvent.click(await screen.findByRole('button', { name: 'На контроль' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('Уже на контроле');
    await userEvent.click(screen.getByRole('button', { name: 'На контроль' }));
    expect(await screen.findByText(/На контроле — пришлём уведомление/)).toBeInTheDocument();
    expect(add).toHaveBeenLastCalledWith({ number: 'RUD-CR.PA08.B.89369/26' });
    expect(screen.queryByRole('button', { name: 'На контроль' })).not.toBeInTheDocument();
  });
});
