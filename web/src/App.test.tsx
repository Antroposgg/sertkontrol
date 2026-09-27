import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it } from 'vitest';

import { App } from './App';
import { ApiContext } from './api/context';
import { SCREENS } from './screens';
import { fakeApi } from './test/fakeApi';

function renderApp() {
  return render(
    <ApiContext.Provider value={fakeApi()}>
      <App />
    </ApiContext.Provider>,
  );
}

describe('App', () => {
  it('всегда показывает пометку тестовых данных', () => {
    renderApp();
    expect(screen.getByRole('note')).toHaveTextContent('Тестовые данные');
  });

  it('переключает экраны', async () => {
    renderApp();
    expect(screen.getByRole('heading', { level: 2 })).toHaveTextContent('Портфель');
    for (const s of SCREENS) {
      await userEvent.click(screen.getByRole('button', { name: s.title }));
      expect(screen.getByRole('heading', { level: 2 })).toHaveTextContent(s.title);
      expect(screen.getByRole('button', { name: s.title })).toHaveAttribute('aria-current', 'page');
    }
  });

  it('без провайдера API — понятная ошибка', () => {
    expect(() => render(<App />)).toThrow('ApiContext.Provider');
  });
});
