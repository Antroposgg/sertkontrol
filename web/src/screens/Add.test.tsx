import { screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';

import { ApiError } from '../api/problem';
import { fakeApi, verdict } from '../test/fakeApi';
import { renderWithApi } from '../test/render';
import { Add } from './Add';

describe('Добавить', () => {
  it('номер → карточка с метками → на контроль', async () => {
    const api = fakeApi();
    renderWithApi(<Add />, api);
    await userEvent.type(screen.getByLabelText('Номер документа'), ' RU Д-CR.PA08.B.89369/26 ');
    await userEvent.type(screen.getByLabelText('SKU (необязательно)'), 'SKU-9');
    await userEvent.click(screen.getByRole('button', { name: 'Проверить' }));
    expect(api.check).toHaveBeenCalledWith('RU Д-CR.PA08.B.89369/26');
    const card = await screen.findByRole('article');
    expect(card).toHaveTextContent('Декларация RU Д-CR.PA08.B.89369/26 — действует');
    for (const section of ['Факт', 'Расчёт', 'Рекомендация']) {
      expect(screen.getByRole('region', { name: section })).toBeInTheDocument();
    }
    expect(card).toHaveTextContent('Заявитель: ООО «ТЕСТ», ИНН 7700000016');
    expect(card).toHaveTextContent('Данные реестра на 25.09.2026');
    expect(card).toHaveTextContent('Тестовые данные');
    await userEvent.click(screen.getByRole('button', { name: 'На контроль' }));
    expect(api.addToPortfolio).toHaveBeenCalledWith({ number: 'RUD-CR.PA08.B.89369/26', sku: 'SKU-9' });
    expect(await screen.findByRole('status')).toHaveTextContent('На контроле');
    expect(screen.queryByRole('button', { name: 'На контроль' })).not.toBeInTheDocument();
  });

  it('нет в данных — ближайшие номера, без ссылки на реестр', async () => {
    const missing = {
      ...verdict,
      level: 'not_found' as const,
      card: null,
      findings: [{ basis: 'fact' as const, rule: 'not_found', text: 'Номера нет в данных реестра на 25.09.2026' }],
      suggestions: [{ number: 'RUD-CR.PA09.B.89369/26', display_number: 'RU Д-CR.PA09.B.89369/26', distance: 1 }],
    };
    renderWithApi(<Add />, fakeApi({ check: vi.fn(() => Promise.resolve(missing)) }));
    await userEvent.type(screen.getByLabelText('Номер документа'), 'RU Д-CR.PA07.B.89369/26');
    await userEvent.click(screen.getByRole('button', { name: 'Проверить' }));
    const card = await screen.findByRole('article');
    expect(card).toHaveTextContent('— нет в данных реестра');
    expect(card).toHaveTextContent('Похожие номера: RU Д-CR.PA09.B.89369/26');
    expect(screen.queryByText('Открыть в реестре')).not.toBeInTheDocument();
  });

  it('PDF → карточка; ошибка распознавания и повтор', async () => {
    const checkFile = vi
      .fn()
      .mockRejectedValueOnce(
        new ApiError({ type: 'x', title: 't', status: 422, detail: 'в PDF не найден номер документа', code: 'number_not_recognized' }),
      )
      .mockResolvedValue([verdict]);
    renderWithApi(<Add />, fakeApi({ checkFile }));
    await userEvent.upload(screen.getByLabelText('PDF-выписка'), new File(['%PDF'], 'a.pdf', { type: 'application/pdf' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('в PDF не найден номер документа');
    await userEvent.click(screen.getByRole('button', { name: 'Повторить' }));
    expect(await screen.findByRole('article')).toBeInTheDocument();
    expect(checkFile).toHaveBeenCalledTimes(2);
  });

  it('пустой результат и ошибка постановки на контроль', async () => {
    const add = vi.fn(() =>
      Promise.reject(new ApiError({ type: 'x', title: 'Уже существует', status: 409, detail: 'уже на контроле', code: 'conflict' })),
    );
    const api = fakeApi({ checkFile: vi.fn(() => Promise.resolve([])), addToPortfolio: add });
    renderWithApi(<Add />, api);
    await userEvent.upload(screen.getByLabelText('PDF-выписка'), new File(['%PDF'], 'a.pdf'));
    expect(await screen.findByText('В файле не найдено номеров.')).toBeInTheDocument();
    await userEvent.type(screen.getByLabelText('Номер документа'), 'x');
    await userEvent.click(screen.getByRole('button', { name: 'Проверить' }));
    await userEvent.click(await screen.findByRole('button', { name: 'На контроль' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('уже на контроле');
  });

  it('кнопка «Проверить» неактивна без номера', () => {
    renderWithApi(<Add />, fakeApi());
    expect(screen.getByRole('button', { name: 'Проверить' })).toBeDisabled();
  });
});

describe('Добавить — «Это номер …?»', () => {
  it('подсказка → «Да, проверить» → карточка точного номера', async () => {
    const question = {
      ...verdict,
      check_id: 5,
      level: 'needs_confirmation' as const,
      number: 'RUD-CB.PAO8.B.89369/26',
      display_number: 'RU Д-CB.PAO8.B.89369/26',
      card: null,
      distance: 1.3,
      suggestions: [{ number: 'RUD-CR.PA08.B.89369/26', display_number: 'RU Д-CR.PA08.B.89369/26', distance: 1.3 }],
    };
    const check = vi.fn().mockResolvedValueOnce(question).mockResolvedValueOnce(verdict);
    renderWithApi(<Add />, fakeApi({ check }));
    await userEvent.type(screen.getByLabelText('Номер документа'), 'ЕАЭС М RU ДСВ.РАО8.В.89369/26');
    await userEvent.click(screen.getByRole('button', { name: 'Проверить' }));
    expect(await screen.findByText('❓ Это номер RU Д-CR.PA08.B.89369/26?')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'На контроль' })).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Да, проверить' }));
    expect(check).toHaveBeenLastCalledWith('RUD-CR.PA08.B.89369/26');
    expect(await screen.findByRole('button', { name: 'На контроль' })).toBeInTheDocument();
  });

  it('на телефоне в MAX — «Сканировать QR»: текст кода уходит в проверку', async () => {
    const qr = 'https://pub.fsa.gov.ru/rds/declaration/view/21950326/common';
    window.WebApp = { platform: 'android', openCodeReader: () => Promise.resolve({ value: qr }) };
    try {
      const api = fakeApi();
      renderWithApi(<Add />, api);
      await userEvent.click(screen.getByRole('button', { name: 'Сканировать QR с выписки' }));
      expect(api.check).toHaveBeenCalledWith(qr);
      expect(await screen.findByRole('article')).toHaveTextContent('Декларация RU Д-CR.PA08.B.89369/26');
    } finally {
      delete window.WebApp;
    }
  });

  it('сканер закрыт без результата — пояснение на экране', async () => {
    window.WebApp = { platform: 'ios', openCodeReader: () => Promise.reject(Object.assign(new Error('cancel'), { error: { code: 'client.open_code_reader.cancelled' } })) };
    try {
      const api = fakeApi();
      renderWithApi(<Add />, api);
      await userEvent.click(screen.getByRole('button', { name: 'Сканировать QR с выписки' }));
      expect(await screen.findByRole('status')).toHaveTextContent('Сканер закрыт без результата (client.open_code_reader.cancelled)');
      expect(api.check).not.toHaveBeenCalled();
    } finally {
      delete window.WebApp;
    }
  });

  it('в вебе кнопки сканера нет — только загрузка PDF', () => {
    window.WebApp = { platform: 'web' };
    try {
      renderWithApi(<Add />, fakeApi());
      expect(screen.queryByRole('button', { name: /Сканировать QR/ })).not.toBeInTheDocument();
      expect(screen.getByLabelText('PDF-выписка')).toBeInTheDocument();
    } finally {
      delete window.WebApp;
    }
  });

  it('F8: ИНН поставщика уходит в постановку, итог сверки — под карточкой', async () => {
    const mismatch = {
      ...verdict,
      level: 'warning' as const,
      findings: [
        ...verdict.findings,
        { basis: 'calculation' as const, rule: 'supplier.mismatch', text: 'Документ оформлен не на поставщика: заявитель — ИНН 7700000016, поставщик — ИНН 7700000023' },
        { basis: 'recommendation' as const, rule: 'advice.check_supplier', text: 'Запросите подтверждение цепочки поставки' },
      ],
    };
    const api = fakeApi({ addToPortfolio: vi.fn(() => Promise.resolve({ item: {} as never, verdict: mismatch })) });
    renderWithApi(<Add />, api);
    await userEvent.type(screen.getByLabelText('Номер документа'), 'RU Д-CR.PA08.B.89369/26');
    await userEvent.type(screen.getByLabelText('ИНН поставщика (необязательно)'), ' 7700000023 ');
    await userEvent.click(screen.getByRole('button', { name: 'Проверить' }));
    await userEvent.click(await screen.findByRole('button', { name: 'На контроль' }));
    expect(api.addToPortfolio).toHaveBeenCalledWith({ number: 'RUD-CR.PA08.B.89369/26', supplier_inn: '7700000023' });
    const notes = await screen.findByRole('list', { name: 'Сверка с поставщиком' });
    expect(notes).toHaveTextContent('заявитель — ИНН 7700000016, поставщик — ИНН 7700000023');
    expect(notes).toHaveTextContent('подтверждение цепочки поставки');
  });
});
