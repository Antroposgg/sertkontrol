import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it } from 'vitest';

import { App } from './App';
import { SCREENS } from './screens';

describe('App', () => {
  it('всегда показывает пометку тестовых данных', () => {
    render(<App />);
    expect(screen.getByRole('note')).toHaveTextContent('Тестовые данные');
  });

  it('переключает экраны', async () => {
    render(<App />);
    expect(screen.getByRole('heading', { level: 2 })).toHaveTextContent('Портфель');
    for (const s of SCREENS) {
      await userEvent.click(screen.getByRole('button', { name: s.title }));
      expect(screen.getByRole('heading', { level: 2 })).toHaveTextContent(s.title);
      expect(screen.getByRole('button', { name: s.title })).toHaveAttribute('aria-current', 'page');
    }
  });
});
