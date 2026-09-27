import { MaxUI } from '@maxhub/max-ui';
import { render } from '@testing-library/react';
import type { ReactNode } from 'react';

import { ApiContext } from '../api/context';
import type { SertkontrolApi } from '../api/sertkontrol';

/** Рендер с провайдерами приложения. */
export function renderWithApi(ui: ReactNode, api: SertkontrolApi) {
  return render(
    <ApiContext.Provider value={api}>
      <MaxUI>{ui}</MaxUI>
    </ApiContext.Provider>,
  );
}
