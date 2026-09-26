import { useState } from 'react';

import { DemoBadge } from './components/DemoBadge';
import { SCREENS, type ScreenId } from './screens';

/** Оболочка мини-приложения: навигация по экранам и пометка тестовых данных. */
export function App() {
  const [screen, setScreen] = useState<ScreenId>('portfolio');
  const current = SCREENS.find((s) => s.id === screen) ?? SCREENS[0];

  return (
    <main>
      <header>
        <h1>Сертконтроль</h1>
        <DemoBadge />
      </header>
      <nav aria-label="Разделы">
        {SCREENS.map((s) => (
          <button key={s.id} type="button" aria-current={s.id === screen ? 'page' : undefined} onClick={() => { setScreen(s.id); }}>
            {s.title}
          </button>
        ))}
      </nav>
      <section aria-labelledby="screen-title">
        <h2 id="screen-title">{current.title}</h2>
        <p>Экран в разработке.</p>
      </section>
    </main>
  );
}
