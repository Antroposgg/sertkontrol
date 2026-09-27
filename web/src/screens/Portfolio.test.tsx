import { screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';

import { ApiError } from '../api/problem';
import type { PortfolioItem } from '../api/types';
import { fakeApi, page } from '../test/fakeApi';
import { renderWithApi } from '../test/render';
import { Portfolio } from './Portfolio';

const problem = { type: 'x', title: 't', status: 0, code: 'network' as const };

describe('Портфель', () => {
  it('загрузка → список', async () => {
    renderWithApi(<Portfolio />, fakeApi());
    expect(screen.getByRole('status')).toHaveTextContent('Загрузка');
    expect(await screen.findByText('RU Д-CR.PA08.B.89369/26')).toBeInTheDocument();
    expect(screen.getByText(/действует · SKU-1 · ИНН 7700000016/)).toBeInTheDocument();
  });

  it('пусто', async () => {
    renderWithApi(<Portfolio />, fakeApi({ listPortfolio: vi.fn(() => Promise.resolve({ items: [], next_cursor: null })) }));
    expect(await screen.findByText(/Документов на контроле нет/)).toBeInTheDocument();
  });

  it('ошибка и повтор', async () => {
    const list = vi.fn().mockRejectedValueOnce(new ApiError(problem)).mockResolvedValue(page);
    renderWithApi(<Portfolio />, fakeApi({ listPortfolio: list }));
    expect(await screen.findByRole('alert')).toHaveTextContent('Нет связи');
    await userEvent.click(screen.getByRole('button', { name: 'Повторить' }));
    expect(await screen.findByText('RU Д-CR.PA08.B.89369/26')).toBeInTheDocument();
  });

  it('фильтры по статусу и ИНН', async () => {
    const api = fakeApi();
    renderWithApi(<Portfolio />, api);
    await screen.findByText('RU Д-CR.PA08.B.89369/26');
    await userEvent.selectOptions(screen.getByLabelText(/Статус/), 'terminated');
    await waitFor(() => {
      expect(api.listPortfolio).toHaveBeenLastCalledWith({ status: 'terminated' });
    });
    await userEvent.type(screen.getByLabelText('ИНН поставщика'), '7700000016');
    await userEvent.click(screen.getByRole('button', { name: 'Найти' }));
    await waitFor(() => {
      expect(api.listPortfolio).toHaveBeenLastCalledWith({ status: 'terminated', supplierInn: '7700000016' });
    });
  });

  it('снять с контроля и ошибка снятия', async () => {
    const remove = vi.fn().mockResolvedValueOnce(undefined).mockRejectedValueOnce(new ApiError(problem));
    const api = fakeApi({ removeFromPortfolio: remove });
    renderWithApi(<Portfolio />, api);
    const button = await screen.findByRole('button', { name: /Снять с контроля/ });
    await userEvent.click(button);
    expect(remove).toHaveBeenCalledWith(7);
    await waitFor(() => {
      expect(api.listPortfolio).toHaveBeenCalledTimes(2);
    });
    await userEvent.click(await screen.findByRole('button', { name: /Снять с контроля/ }));
    expect(await screen.findByRole('alert')).toHaveTextContent('Нет связи');
  });

  it('показать ещё по курсору; ошибка догрузки; смена фильтра сбрасывает догруженное', async () => {
    const first = { items: page.items, next_cursor: '7' };
    const second = { items: [{ ...page.items[0], id: 8, display_number: 'RU Д-2/26' } as PortfolioItem], next_cursor: null };
    const list = vi
      .fn()
      .mockResolvedValueOnce(first)
      .mockRejectedValueOnce(new ApiError(problem))
      .mockResolvedValueOnce(second)
      .mockResolvedValue({ items: page.items, next_cursor: null });
    renderWithApi(<Portfolio />, fakeApi({ listPortfolio: list }));
    await userEvent.click(await screen.findByRole('button', { name: 'Показать ещё' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('Нет связи');
    expect(list).toHaveBeenLastCalledWith({ cursor: '7' });
    await userEvent.click(screen.getByRole('button', { name: 'Показать ещё' }));
    expect(await screen.findByText('RU Д-2/26')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Показать ещё' })).not.toBeInTheDocument();
    await userEvent.selectOptions(screen.getByLabelText(/Статус/), 'active');
    await waitFor(() => {
      expect(screen.queryByText('RU Д-2/26')).not.toBeInTheDocument();
    });
    expect(list).toHaveBeenLastCalledWith({ status: 'active' });
  });

  it('открыть документ', async () => {
    const open = vi.fn();
    renderWithApi(<Portfolio openDocument={open} />, fakeApi());
    await userEvent.click(await screen.findByRole('button', { name: /Открыть RU Д-CR/ }));
    expect(open).toHaveBeenCalledWith('RU Д-CR.PA08.B.89369/26');
  });
});
