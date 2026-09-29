import { MaxUI } from '@maxhub/max-ui';
import { useState } from 'react';

import { ConsentGate } from './components/ConsentGate';
import { DemoBadge } from './components/DemoBadge';
import { getStartTarget, getWebApp } from './max/bridge';
import { Add } from './screens/Add';
import { Data } from './screens/Data';
import { Document } from './screens/Document';
import { Import } from './screens/Import';
import { Portfolio } from './screens/Portfolio';
import { SCREENS, type ScreenId } from './screens';

/** Навигация, доступная экранам. */
interface ScreenProps {
  /** Номер для экрана «Документ». */
  documentNumber: string;
  /** Открыть экран «Документ» с номером. */
  openDocument: (number: string) => void;
}

const CONTENT: Record<ScreenId, (props: ScreenProps) => React.JSX.Element> = {
  portfolio: ({ openDocument }) => <Portfolio openDocument={openDocument} />,
  // key — новый номер пересоздаёт экран вместе с полем ввода.
  document: ({ documentNumber }) => <Document key={documentNumber} number={documentNumber} />,
  add: () => <Add />,
  import: () => <Import />,
  data: () => <Data />,
};

/** Платформа для MAX UI: iOS-оформление на iOS, иначе Android-оформление. */
function platform(): 'ios' | 'android' {
  return getWebApp()?.platform === 'ios' ? 'ios' : 'android';
}

/** Оболочка мини-приложения: навигация по экранам и пометка тестовых данных. */
export function App() {
  // Кнопка бота «Подробнее» открывает сразу нужный документ (`start_param`).
  const [start] = useState(() => getStartTarget());
  const [screen, setScreen] = useState<ScreenId>(start?.screen ?? 'portfolio');
  const [documentNumber, setDocumentNumber] = useState(start?.screen === 'document' ? start.number : '');
  const current = SCREENS.find((s) => s.id === screen) ?? SCREENS[0];
  const Content = CONTENT[current.id];

  return (
    <MaxUI platform={platform()}>
      <main>
        <header>
          <h1>Сертконтроль</h1>
          <DemoBadge />
        </header>
        <ConsentGate>
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
            <Content
              documentNumber={documentNumber}
              openDocument={(number) => {
                setDocumentNumber(number);
                setScreen('document');
              }}
            />
          </section>
        </ConsentGate>
      </main>
    </MaxUI>
  );
}
