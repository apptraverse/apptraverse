# Системные настройки, доступность и единая раскладка AppTraverse

Дата согласования: 16 ноября 2026 года. Пример проверки: `examples/window_catalog`.
Минимальная Windows: **Windows 7**. Статус: первый Windows-срез реализован и
проверен в объёме validation ниже; полная accessibility-матрица и перенос
остальных backend остаются этапами плана.

Уточнение пользователя: сохранённый снимок ОС и предпочтения устройства теперь
являются штатным **client-local объектным состоянием**. На старте и при системных
изменениях сравнение порождает локальные Events; разрешение мониторов окон тоже
выполняется Events. Это уточнение заменяет прежнее требование не сохранять
наблюдения ОС. Расчётные метрики, HWND и layout cache остаются runtime-only.
Контракт: [Client-local environment](architecture/ClientLocalEnvironment.md).

Текущие свидетельства и ограничения: [Windows layout/settings validation](validation/window-catalog-layout-settings-2026-11-16.md).

Этот план закрепляет требование пользователя: один layout engine AppTraverse
на всех платформах; constraints входят в дистиллированную поставку, изменяются
в runtime через модель и могут обновляться новой ревизией поставки.
Сейчас заказана реализация Windows WindowCatalog. Остальные платформенные
адаптеры остаются этапами плана, а не объявляются реализованными.

## 1. Граница ответственности

| Данные или работа | Владелец |
| --- | --- |
| Состав UI, подписи, доступность действий, выбранные значения, constraints | Переносимая UI-модель |
| Светлая/тёмная тема, системная палитра, шрифты и предпочтения доступности | Локальное окружение представления |
| Viewport, масштаб конкретного окна, измерения и рассчитанные прямоугольники | Runtime представления |
| Размещение, распределение свободного места, переполнение | Общий layout engine AppTraverse |
| Измерение нативного текста/контролов, отображение и ввод | Платформенный backend и презентеры |

Модель задаёт намерение и ограничения, а локальное представление рассчитывает
геометрию. Расчётчик — общая платформонезависимая библиотека, исполняемая рядом
с UI mirror. Изменение размера окна, DPI, системного шрифта или масштаба текста
само по себе не требует round trip в model thread или на другое устройство.

Системные настройки, нативные handles, фокус, метрики, результаты расчёта и кеши
не входят в переносимый USER. Снимок параметров ОС и локальные предпочтения
сохраняются в LOCAL через Events; handles, фокус, измерения, результаты расчёта
и кеши не сохраняются. Изменение смыслового параметра раскладки пользователем, например
пропорции панелей, может сохраняться моделью через обычный Event.

Сохраняется принятый путь действий:

`GUI / accessibility action → ModelObjectProxy → Event → model → publication → UI mirror → presenters`.

## 2. Единый layout engine

Для одной области UI геометрией владеет только AppTraverse. Нельзя одновременно
передавать нашим контролам прямоугольники и поручать независимый перерасчёт этой
же области NSLayoutConstraint, Android Layout либо CSS Flex/Grid. Нативный
контрол продолжает самостоятельно рисовать свой текст, рамку и содержимое.

Декларативное описание должно поддерживать:

- горизонтальные и вертикальные контейнеры, привязки и ограничения размеров;
- минимальные, предпочтительные и максимальные размеры, растяжение и промежутки;
- системные отступы и intrinsic size, полученные от backend;
- приоритеты ограничений с определённым поведением при нехватке места;
- явную политику переполнения: перестроение, перенос или прокрутку;
- логические единицы и одно преобразование в нативные координаты на границе backend.

Первый вертикальный срез может реализовывать ограниченный документированный
набор контейнерных constraints. Он не должен называться универсальным решателем
произвольных линейных уравнений, пока такой решатель и его проверки не реализованы.
API не должен знать о WindowCatalog, номерах его кнопок и прикладных классах.

В текущем Windows-срезе выбран именно структурированный набор: leaf,
horizontal/vertical/adaptive flow, min/preferred/max, grow, padding, gap и
alignment. Обязательные minimum/maximum отличаются от сжимаемого preferred size.
Произвольные anchor-уравнения и числовые приоритеты в этот срез не входят.
Остальные backend пока сохраняют прежнюю раскладку до своих этапов переноса;
добавление общей библиотеки само по себе не означает их автоматический перевод.

Текст с переносом требует измерения с ограничением ширины. Раскладка выполняет
measure/arrange и при необходимости ограниченное уточнение размеров. Изменение
шрифта сбрасывает кеш измерений; изменение только цвета вызывает перерисовку.
Не допускается уменьшать accessibility-текст для выполнения фиксированной
геометрии. При маленьком viewport действует выбранная политика переполнения.

## 3. Дистилляция, runtime и обновления

В поставку входят значения constraints и ссылки на элементы. В неё не входят
рассчитанные координаты для конкретного шрифта или экземпляра устройства.
Дистилляция проверяет структуру, диапазоны параметров и обязательные связи.
Взаимные ограничения сами по себе не считаются ошибкой: ошибка определяется
контрактом реализованного решателя, а не наличием произвольного цикла ссылок.

| Источник изменения | Поведение |
| --- | --- |
| Дистилляция prefab | Создаёт исходный набор правил |
| Runtime Event модели | Согласованно изменяет правила; UI mirror получает публикацию и пересчитывается |
| Новая ревизия поставки | Меняет исходные правила по обычному supply/update-контракту |
| DPI, шрифт, тема, viewport | Сразу меняют локальное отображение; сравнение OS-снимка создаёт LOCAL Events, не меняя переносимые constraints |

Обновление нескольких связанных правил публикуется согласованным набором.
Нельзя представлять полуобновлённую раскладку между двумя зависимыми изменениями.
Ошибочные ограничения получают конкретную диагностику; обязательные правила
нельзя незаметно пропускать в презентере.

Политика обновлений отделяет новые исходные значения поставки от явных USER
переопределений. Обновление исходной поставки не является Event, меняющим
неизменяемую supply-константу на месте. Механизм обновления должен проверяться
на двух независимо дистиллированных ревизиях, а runtime-переопределение — отдельным
сценарием. До исполнения такой проверки статус обновлений остаётся NOT RUN.

Первый срез представляет правила через supply-owned `UiLayoutDefinition` со
стабильной identity и динамический `UiLayoutState`, хранящий ссылку на definition
и явный override. Без override выбирается definition новой совместимой поставки;
override переживает замену поставки; ClearOverride снова выбирает текущую
definition. Эта политика не обещает автоматическую миграцию несовместимой схемы
или сетевую доставку обновлений. `TrySetOverride` отклоняет некорректное дерево
до Commit, сохраняя предыдущее состояние и журнал без изменений.

Для текущего прототипа соблюдается S0-only схема приложения из AGENTS.md:
поля добавляются в текущие Load/Save; старые экспериментальные профили не
мигрируются и не сбрасываются. Все вновь создаваемые платформенные supplies
должны иметь одну текущую переносимую схему. USER проверяется только с подходящей
ревизией поставки; исходные профили сохраняются.

## 4. Автоматическая тема и системные метрики

Общий runtime-контекст представления автоматически устанавливается application
host. Он предоставляет семантическую палитру: фон, текст, выделение, границы,
фокус и disabled-состояние; шрифты; предпочтения контраста, движения,
прозрачности и другие реально поддерживаемые платформой параметры.

Host подписывается на системные уведомления и доставляет изменения в UI thread.
Подписки удаляются при завершении presentation lifecycle. Нужны корректные
обновления уже показанных и вновь создаваемых окон без периодического polling.
Отсутствие платформенной возможности явно обозначается как отсутствие поддержки,
а не как ложное значение «настройка выключена».

Нативные стили используются по умолчанию. Собственная отрисовка получает цвета,
метрики, состояния и focus indicator через backend. Растровый фон кнопки не
заменяет поддержку темы или High Contrast. В контрастном режиме приоритет имеют
цвета пользователя. Данные нельзя различать только цветом.

## 5. Семантика и доступность

Стандартные компоненты автоматически предоставляют роль, название из
локализованной подписи, доступные действия, enabled/selected/value и применимые
уведомления. При необходимости модель предоставляет отдельное доступное название,
описание и связь с подписью. Для кнопок без текстовой подписи разработчик обязан
задать смысл действия; библиотека не может угадать его из изображения.

Клавиатурный фокус не равен model selection. Нужны Tab/Shift+Tab, стандартная
активация, навигация внутри списка, видимый фокус и восстановление фокуса после
удаления строки или закрытия окна. Публикация модели не должна перехватывать фокус.
Accessibility queries читают согласованный UI mirror; действия идут тем же
ModelObjectProxy-путём и выполняются один раз. Настройки скринридера не служат
условием включения семантики: она предоставляется всегда.

## 6. Windows, начиная с Windows 7

Первый этап подключает общую раскладку к главным и дочерним окнам WindowCatalog,
сохраняя нативные BUTTON/LISTBOX, model-first действия и lifecycle/persistence.
Backend измеряет текст системным шрифтом, переводит логические единицы в пиксели
и применяет готовую геометрию. Собственная модель не рассчитывает платформенные
прямоугольники.

Совместимый базовый путь использует доступные Win7 API: системный DPI, системные
цвета и шрифты, High Contrast, клавиатуру и нативную accessibility-инфраструктуру.
Новые Windows API допускаются только как обнаруживаемые runtime-возможности.
В таблице импорта исполняемого файла не должно быть обязательных Win10-only
функций. Например, GetDpiForWindow нельзя вызывать безусловно; для старой системы
нужен системный DPI. Это не обещание per-monitor DPI на Windows 7.

Современная Windows отдельно получает per-monitor DPI и отдельный масштаб текста
через доступные ей API. Наличие слайдера «Размер текста» новых Windows и общей
системной тёмной темы не предполагается для Win7. Контрастная тема, системная
палитра и readable layout обязательны для совместимого пути.

Проверка Win7 включает реальные запуск и работу на Win7, а также аудит импортов
и зависимостей. Определение _WIN32_WINNT или успешная сборка на новой Windows
не считаются доказательством совместимости.

Toolchain также является частью этого контракта. Для Win7 используется
совместимый v143/MSVC 14.44; MSVC 14.50/Visual Studio 2026 не подходит как
базовый toolset. В исследованной сборке его готовая STL filesystem добавляла
обязательные CreateFile2/CopyFile2 независимо от совместимых Win32 вызовов
самого backend. Новый backend поэтому проверяется вместе с реальным PE import
table и версиями подсистемы, а не только поиском API в исходниках. Для Debug
на целевой машине дополнительно нужен соответствующий совместимый Debug CRT.

## 7. Следующие платформенные этапы

| Платформа | Адаптер и особенности |
| --- | --- |
| Linux / GTK | GTK-тема и шрифты, настройки движения, нативное измерение, общий layout, AT-SPI/Orca; X11/Wayland проверяются отдельно |
| Android | Убрать принудительную Light-тему и ручную палитру строк; тематические attrs, SP и платформенное нелинейное измерение; общий layout, configuration changes, TalkBack |
| Web | Общий layout применяет прямоугольники в DOM; CSS отвечает за оформление; color-scheme, forced-colors, поддерживаемые media queries; keyboard/focus/ARIA списка, zoom/reflow |
| macOS | Убрать preferredColorScheme(.light), белые фоны и фиксированные шрифты; нативные метрики и системные цвета; общий layout, VoiceOver, keyboard |
| iOS | Dynamic Type, системные цвета и другие доступные preferences; нативное измерение и общий layout, VoiceOver, Switch Control и аппаратная клавиатура |

Порядок: переносимый контракт и тесты → Windows → Web → GTK → Android →
macOS/iOS → итоговая матрица. Нативные контейнеры могут использоваться внутри
контрола, но не получают второй независимый расчёт внешней геометрии.

## 8. Приёмка на WindowCatalog

Одинаковый сценарий на каждой платформе: открыть каталог, создать окна,
выбрать и открыть строку, закрыть/повторно открыть окно, удалить строку,
сменить язык, сохранить и холодно загрузить состояние.

Проверки нового поведения:

1. Constraints берутся из действительно дистиллированной поставки, а не
   восстанавливаются скрытой прикладной веткой в презентере.
2. Runtime Event меняет правила; публикация меняет геометрию. После Save/Load
   явное сохраняемое изменение воспроизводится с той же схемой.
3. Две поставки с разными исходными правилами проверяют договорённую политику
   обновления; USER-переопределение и значения поставки не смешиваются.
4. При изменении окна, шрифта и DPI UI пересчитывается локально. Нет обрезания
   текста, перекрытий и потери доступных действий на узком viewport.
5. Light/Dark/High Contrast проверяются для normal/selected/focused/pressed/
   disabled, а новые окна сразу получают актуальные настройки.
6. Клавиатура и реальный скринридер выполняют полный сценарий. Инспектор дерева
   проверяет названия/роли/состояния, но не заменяет работу скринридера.
7. Два сеанса с одним логическим каталогом и разными настройками имеют разную
   местную геометрию, не меняя системные предпочтения друг друга.
8. Повторяются затронутые headless, native acceptance, persistence/lifecycle и
   межплатформенные USER-проверки в ранее подтверждённом объёме.

Автоматические unit-тесты проверяют generic layout независимо от WindowCatalog:
распределение места, измерения, ограничения, переполнение и детерминированность.
Модельные тесты проверяют дистилляцию, Events, replay и отсутствие системных
метрик в USER. Нативные тесты проверяют фактическую геометрию, действия и teardown.
Скриншоты и эмуляция предпочтений браузера дополняют, а не заменяют runtime.

## 9. Статус текущего задания

Ниже фиксируется именно новая реализация, не исторические PASS прежних работ.
Таблица обновляется исполнителем только после соответствующего исполнения.

| Область | Статус на момент составления плана |
| --- | --- |
| План и архитектурный контракт | Сохранены этим документом |
| Общий structured layout engine и его unit-тесты | PASS на GCC Windows/Linux; не означает перенос native backend этих платформ |
| Generic UiLayoutState: native distillation, Event/replay, cold user.bin, смена совместимой поставки A→B, ClearOverride, proxy/publication, atomic validation | PASS: `apptraverse_ui_layout_state_test`, пять сценариев, 16 ноября 2026 |
| Прикладные WindowCatalog правила / runtime Event / USER | PASS: model/headless/USER/supply/replay и native runtime override в выбранном объёме |
| Windows WindowCatalog: нативное измерение и общий layout | v143 build 23 targets PASS; 23 различных теста PASS совокупно, scrollbar-пакет 7/7 и финальный keyboard/native-пакет 2/2; первоначальные FAIL и исправления описаны в validation |
| Windows: системные настройки и keyboard/accessibility | Проверены текущая тёмная схема/шрифт, Tab и Return, создание/закрытие окон и cold restore vertical layout; полный SR/HC/text-scale сценарий NOT RUN |
| Win7 реальный запуск | NOT RUN; требуется Win7-окружение |
| Windows реальный скринридер | NOT RUN |
| Новые Linux/Android/Web/macOS/iOS адаптеры | PLANNED |
| Повторная межплатформенная runtime-приёмка новой версии | NOT RUN |

## 10. Критерий автоматической поддержки

Независимая тестовая композиция стандартных UI-компонентов получает окружение,
измерение, раскладку и доступность через обычный application host без ссылок
на WindowCatalog. Прикладные презентеры не следят за DPI, темой и системным
размером текста. Собственные контролы используют те же публичные API.

Полной поддержкой считается выполненная матрица соответствующей платформы,
а не наличие интерфейсов или успешная компиляция адаптера. Старые реальные
проверки сохраняются с исходным объёмом; неподтверждённые возможности получают
NOT RUN/BLOCKED с конкретной причиной.


## 11. Final authorized non-Apple scope — 2026-11-17

The user authorized Windows/common corrections and Linux/GTK, Web and Android
implementation, including independent MinimalApp adoption. These non-Apple
adapters are implemented within the executed scopes below; the earlier §9 table
is the original Windows checkpoint. Apple-specific implementation remains excluded.
GPT-6 Astra ultra performed the read-only review and recorded dispositions for all
30 mandatory Canon rules; GPT-6.1 Sol high owned all coding. Closing that scoped
source audit is not certification of every possible runtime or OS environment.

| Current scope | Recorded outcome |
| --- | --- |
| Windows/common model, native lifecycle, independent HWND drag/resize/publication/cold restore, same-target request regression | 29 distinct final test outcomes PASS; four PE import inspections PASS within static scope |
| Linux/GTK and Web | Real GUI/browser and lifecycle scopes PASS; native Windows USER transfers with absent/existing LOCAL PASS; actual compatible supply/default/override/Clear update PASS |
| Android transfers and deployment | Portable-only, absent LOCAL seed and existing LOCAL transfers PASS; actual APK/supply update/default-versus-override PASS |
| Android environment and generic composition | Live/stopped OS settings and unchanged-startup comparison PASS; completed missing-binding negative PASS; MinimalApp and ordinary Debug fresh-policy checks PASS |
| Android functional UI | Seven distinct baseline cases plus the new native disabled-state case PASS; two Home/Back then native Recents-dismiss cases NOT PASS; no blanket 9/9 claim |
| Android final palette-only appearance/disabled-state regression | PASS, 2/2 narrow cases in 52.929s; native disabled text state and LOCAL cold restore |

The Recents failure was reproduced independently on stock Android Settings on the
same emulator with validated thumbnail geometry and genuine native touch input.
This supports a launcher/input acceptance blocker rather than an established
Catalog-specific regression, and does not count as successful native dismissal.
The separately verified onStop persistence/cold-load boundary remains distinct.

Evidence:
[Windows/common corrections](validation/nonapple-layout-local-canon-fixes-2026-11-16.md),
[Linux/Web](validation/window-catalog-linux-web-local-layout-2026-11-17.md),
[Android](validation/window-catalog-android-settings-2026-11-17.md), and the
[30-rule Canon audit](validation/windows-canon-ultra-audit-2026-11-16.md).

No actual OS-image replacement/OTA was executed; density/font/night-mode changes
and application/supply updates do not establish OS-image-upgrade compatibility.
Actual Win7, physical mixed-DPI/multiple-monitor acceptance, real screen-reader and
full High Contrast/accessibility matrices, Apple implementation/runtime, other
unexecuted environments and Release packaging remain NOT RUN. Platform reports
retain additional limits such as Xvfb versus Wayland and headless browser tab-hide.
The historical 52-process portability matrix was not rerun under this new schema.
