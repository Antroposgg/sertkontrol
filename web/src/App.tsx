import { MaxUI } from '@maxhub/max-ui';
import { useState } from 'react';

import { DemoBadge } from './components/DemoBadge';
import { getWebApp } from './max/bridge';
import { Add } from './screens/Add';
import { Data } from './screens/Data';
import { Portfolio } from './screens/Portfolio';
import { SCREENS, type ScreenId } from './screens';

const CONTENT: Record<ScreenId, () => React.JSX.Element> = { portfolio: Portfolio, add: Add, data: Data };

/** Платформа для MAX UI: iOS-оформление на iOS, иначе Android-оформление. */
function platform(): 'ios' | 'android' {
  return getWebApp()?.platform === 'ios' ? 'ios' : 'android';
}

/** Оболочка мини-приложения: навигация по экранам и пометка тестовых данных. */
export function App() {
  const [screen, setScreen] = useState<ScreenId>('portfolio');
  const current = SCREENS.find((s) => s.id === screen) ?? SCREENS[0];
  const Content = CONTENT[current.id];

  return (
    <MaxUI platform={platform()}>
      <main>
        <header>
          <h1>Сертконтроль</h1>
          <DemoBadge />
        </header>
        <nav aria-label="Разделы">
          {SCREENS.map((s) => (
            <button
              key={s.id}
              type="button"
              aria-current={s.id === screen ? 'page' : undefined}
              onClick={() => {
                setScreen(s.id);
              }}
            >
              {s.title}
            </button>
          ))}
        </nav>
        <section aria-labelledby="screen-title">
          <h2 id="screen-title">{current.title}</h2>
          <Content />
        </section>
      </main>
    </MaxUI>
  );
}
