import { screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';

import { ApiError } from '../api/problem';
import { fakeApi } from '../test/fakeApi';
import { renderWithApi } from '../test/render';
import { Import } from './Import';

const csv = 'SKU;Номер;ИНН\nЧАЙ-01;RU Д-CR.PA08.B.89369/26;7700000023\n';

describe('Импорт', () => {
  it('CSV → текст файла уходит в импорт, отчёт по строкам', async () => {
    const api = fakeApi();
    renderWithApi(<Import />, api);
    await userEvent.upload(screen.getByLabelText('CSV-файл'), new File([csv], 'portfolio.csv', { type: 'text/csv' }));
    expect(await screen.findByText('Поставлено на контроль')).toBeInTheDocument();
    expect(api.importPortfolio).toHaveBeenCalledWith(csv);
    expect(screen.getByRole('region', { name: /Нет в данных реестра/ })).toHaveTextContent('Строка 3: RU Д-XX.0000.A.99999/26');
    expect(screen.getByRole('region', { name: 'Документ оформлен не на поставщика' })).toHaveTextContent(
      'Строка 2: RU Д-CR.PA08.B.89369/26',
    );
    expect(screen.getByRole('region', { name: 'Не поставлены' })).toHaveTextContent('Строка 5: номер документа не распознан');
  });

  it('пустые разделы не показываются', async () => {
    const api = fakeApi({
      importPortfolio: vi.fn(() =>
        Promise.resolve({ total: 1, added: 1, already: 0, not_found: [], supplier_mismatch: [], invalid: [] }),
      ),
    });
    renderWithApi(<Import />, api);
    await userEvent.upload(screen.getByLabelText('CSV-файл'), new File([csv], 'p.csv'));
    expect(await screen.findByText('Поставлено на контроль')).toBeInTheDocument();
    expect(screen.queryByRole('region', { name: 'Не поставлены' })).not.toBeInTheDocument();
    expect(screen.queryByRole('region', { name: /Нет в данных/ })).not.toBeInTheDocument();
  });

  it('ошибка файла — понятное сообщение и «Повторить» с тем же файлом', async () => {
    const importPortfolio = vi
      .fn()
      .mockRejectedValueOnce(
        new ApiError({ type: 'x', title: 'Файл слишком большой', status: 413, code: 'file_too_large', detail: 'в файле импорта больше 1000 строк' }),
      )
      .mockResolvedValue({ total: 1, added: 1, already: 0, not_found: [], supplier_mismatch: [], invalid: [] });
    const api = fakeApi({ importPortfolio });
    renderWithApi(<Import />, api);
    await userEvent.upload(screen.getByLabelText('CSV-файл'), new File([csv], 'big.csv'));
    expect(await screen.findByRole('alert')).toHaveTextContent('больше 1000 строк');
    await userEvent.click(screen.getByRole('button', { name: 'Повторить' }));
    expect(await screen.findByText('Поставлено на контроль')).toBeInTheDocument();
    expect(importPortfolio).toHaveBeenCalledTimes(2);
  });
});
