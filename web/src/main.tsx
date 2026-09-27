import '@maxhub/max-ui/dist/styles.css';
import './styles.css';

import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { App } from './App';
import { createApiClient } from './api/client';
import { ApiContext } from './api/context';
import { createSertkontrolApi } from './api/sertkontrol';
import { getInitData } from './max/bridge';

const root = document.getElementById('root');
if (root === null) {
  throw new Error('Нет элемента #root');
}
const api = createSertkontrolApi(createApiClient({ getInitData: () => getInitData() }));
createRoot(root).render(
  <StrictMode>
    <ApiContext.Provider value={api}>
      <App />
    </ApiContext.Provider>
  </StrictMode>,
);
