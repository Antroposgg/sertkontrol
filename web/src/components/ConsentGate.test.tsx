import { screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';

import { ApiError } from '../api/problem';
import { fakeApi } from '../test/fakeApi';
import { renderWithApi } from '../test/render';
import { ConsentGate } from './ConsentGate';

const me = (consented: boolean) => ({ max_user_id: 1, portfolio_count: 0, is_demo: true, demo_stage: 'base' as const, consented });

describe('ConsentGate', () => {
  it('с согласием показывает приложение', async () => {
    renderWithApi(<ConsentGate>разделы</ConsentGate>, fakeApi());
    expect(await screen.findByText('разделы')).toBeInTheDocument();
  });

  it('без согласия — текст и «Согласен», затем приложение', async () => {
    const meFn = vi.fn().mockResolvedValueOnce(me(false)).mockResolvedValue(me(true));
    const api = fakeApi({ me: meFn });
    renderWithApi(<ConsentGate>разделы</ConsentGate>, api);
    expect(await screen.findByText(/хранит ваш ID в MAX/)).toBeInTheDocument();
    expect(screen.queryByText('разделы')).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Согласен' }));
    expect(api.consent).toHaveBeenCalledOnce();
    expect(await screen.findByText('разделы')).toBeInTheDocument();
  });

  it('ошибка согласия видна, повтор возможен', async () => {
    const api = fakeApi({
      me: vi.fn(() => Promise.resolve(me(false))),
      consent: vi.fn(() => Promise.reject(new ApiError({ type: 'x', title: 't', status: 0, code: 'network' }))),
    });
    renderWithApi(<ConsentGate>разделы</ConsentGate>, api);
    await userEvent.click(await screen.findByRole('button', { name: 'Согласен' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('Нет связи');
    await waitFor(() => {
      expect(screen.getByRole('button', { name: 'Согласен' })).toBeEnabled();
    });
  });

  it('ошибка загрузки профиля — «Повторить»', async () => {
    renderWithApi(<ConsentGate>разделы</ConsentGate>, fakeApi({ me: vi.fn(() => Promise.reject(new Error('сбой'))) }));
    expect(await screen.findByRole('alert')).toHaveTextContent('сбой');
  });
});
