# WindowCatalog: единая раскладка и системные настройки Windows

Дата: 16 ноября 2026 года. План: [системные настройки и доступность](../system-settings-accessibility-plan.md).

## Реализованный объём

Добавлена платформонезависимая библиотека структурированных constraints:
`UiLayoutNode` и `ComputeUiConstraintLayout`. Она рассчитывает размещение локально
в представлении. Windows-презентеры измеряют фактические шрифты/контролы, передают
логические размеры расчётчику и применяют его прямоугольники к нативным HWND.
Изменение viewport не требует публикации модели для перераскладки.

Текущий набор: leaf, horizontal/vertical/adaptive flow, minimum/preferred/maximum,
grow, padding, gap, alignment, измерение при ограниченной ширине и content extent
для переполнения. Это не универсальный решатель произвольных anchor-уравнений;
числовые приоритеты и произвольные зависимости не заявлены реализованными.

`UiLayoutDefinition` — исходное дерево правил поставки со стабильной identity.
`UiLayoutState` — сохраняемая ссылка на definition и явное runtime-переопределение.
Set/Clear проходят через Events; некорректное дерево можно отклонить атомарно
через `TrySetOverride` до изменения журнала. При замене совместимой поставки
без override выбираются новые исходные правила; override сохраняется до Clear.
Это проверка подстановки совместимого BACKING, не новая система доставки
обновлений и не миграция старой схемы.

Windows WindowCatalog получает constraints из общего prefab. Контекстное меню
главного окна позволяет выбрать автоматическую, горизонтальную или вертикальную
раскладку через модель. Меняются правила и геометрия, а не HWND контролов.
Нативные BUTTON/LISTBOX сохраняют стандартные роли и действия.

Локальное Windows-окружение предоставляет системный DPI, message font, цвета,
High Contrast и доступные предпочтения современных Windows. Подписки на
изменения и WM_SETTINGCHANGE/WM_THEMECHANGED/WM_SYSCOLORCHANGE запускают refresh.
Новые DPI/WinRT функции разрешаются динамически. Современная тёмная схема
поддерживается owner-draw оформлением настоящих BUTTON; High Contrast использует
системные цвета и нативное оформление. Добавлена клавиатурная навигация.

NONCLIENTMETRICS message font уже отражает системное увеличение текста в
используемом Windows-пути; дополнительное умножение на TextScaleFactor удалено,
чтобы не увеличивать текст дважды. Сохраняется преобразование system DPI →
window DPI. Само измерение при нестандартном значении системного слайдера
в этой проверке пока не выполнено.

## Схема, владение и профили

Приложение остаётся S0-only. Появились новые ссылки и поля текущей схемы,
поэтому используются новые изолированные профили и заново дистиллированные
supplies. Старые экспериментальные USER не мигрируются, не удаляются и не
исправляются автоматически. Межплатформенные supplies новой версии должны
содержать одну согласованную схему.

Определение раскладки остаётся ссылкой на поставку при historical capture,
replay и compaction. Оно не должно замораживаться в частной копии historical base.
Layout state дочернего окна входит в clone-owned граф prefab, а definition
остаётся parent-owned ссылкой. Перед исторической гидратацией дочернего Domain
parent-owned definition записывается в BACKING; скрытого восстановления ссылок
в презентере нет.

Нативные метрики, DPI, фокус и результирующие прямоугольники не добавляются
в USER. Windows resize не переписывает model viewport/поля расположения
презентеров ради локальной раскладки; существующее сохранение desktop frame
остаётся отдельным поведением.

## Windows 7 и toolchain

Минимальная Windows — 7. Совместимая сборка использует **v143/MSVC 14.44**,
целевые WINVER/_WIN32_WINNT/_STL_WIN32_WINNT 0x0601 и subsystem/OS 6.01.
MSVC 14.50/Visual Studio 2026 не используется для этой цели: его готовая
filesystem-библиотека добавляла hard imports CreateFile2/CopyFile2. Одни
целевые макросы этот импорт не устраняли.

В прямой таблице импортов сборки v143 проверены CreateFileW/CopyFileW и отсутствие
обязательных современных DPI/WinRT/WaitOnAddress-функций. Новые функции backend
загружает по наличию. Локальное свидетельство: `C:/Users/nickc/Documents/Codex/build-window-layout-win7/window-catalog-imports.txt`.

Отдельная configure-проверка отклонения MSVC_VERSION >= 1950 прошла PASS:
несовместимый toolset не принимается как Win7-сборка. Свидетельство:
`C:/Users/nickc/Documents/Codex/build-window-layout-win7/toolchain-rejection.txt`.

Эта проверка подтверждает прямой PE import boundary, а не полный транзитивный
аудит всех DLL и не запуск на Win7. Debug-приложению нужен соответствующий
совместимый Debug CRT. Реальный запуск на Windows 7 остаётся NOT RUN.

## Выполненные проверки

| Проверка | Результат и граница |
| --- | --- |
| Generic layout engine, GCC на Windows и Linux | PASS: переносимая реализация и unit-сценарии; не проверка native adapter Linux |
| `apptraverse_ui_layout_state_test` | PASS: пять перечисленных ниже сценариев |
| WindowCatalog headless, включая clone ownership/replay/cold load layout state | PASS в отдельном выполнении; окончательный набор повторных тестов ниже |
| Debug Windows demo и независимый `--distill-only` | PASS: сформирован новый embedded supply header и native BACKING |
| Прямая PE-таблица импортов v143 | PASS в описанном выше объёме |
| Реальный Windows GUI на текущем host | PASS: текущая тёмная схема/системный шрифт, создание и закрытие окон, vertical через меню, cold restore закрытого Window 1 и vertical, Tab→Add→Return создаёт Window 2 |
| Выбранная MSVC v143 Debug-сборка, 23 targets | PASS: `C:/Users/nickc/Documents/Codex/build-window-layout-win7` |
| Первый выбранный CTest-набор | 21/22 PASS; chrome-тест обнаружил описанную ниже ошибку локальности |
| Отдельная реальная native acceptance WindowCatalog | PASS, 15.28 s |
| Исправление локальности settings refresh и итоговый затронутый пакет | PASS, 7/7, 21.74 s; native acceptance 15.41 s, chrome 3.97 s |
| Итоговый выбранный объём | 23 различных теста PASS совокупно по описанным ниже исполнениям |
| Финальный чистый повтор message host/native acceptance после исправления Return | PASS, 2/2, 16.12 s: native 15.65 s, host keyboard 0.42 s |

Пять сценариев `apptraverse_ui_layout_state_test`:

1. Действительная native distillation и загрузка полного дерева с проверкой
   всех полей, включая вложенные ограничения и enum-значения.
2. SetOverride → журнал → replay → физический user.bin → новая RAM storage и
   новый Domain; override и журнал воспроизводятся.
3. Две независимо созданные supplies A/B с одной definition identity: без
   override выбирается B; override переживает B; ClearOverride выбирает B,
   включая replay и холодный Domain. BACKING обеих поставок не меняется;
   definition не записывается в USER.
4. ModelObjectProxy действительно ставит действие в очередь; UI mirror не
   меняется до model execution и следующей publication. UI journal пуст.
5. Некорректные gap/повторяющиеся control slots отклоняются до Commit:
   текущие правила, override flag, generation и journal остаются прежними.

Новый Windows chrome-тест проверяет реальную геометрию HWND и метрики
действующего шрифта, отсутствие перекрытий, локальный resize до model pump,
runtime override/Clear через proxy без пересоздания HWND и синтетические
уведомления настроек без изменения модели. Синтетические уведомления не
подменяют реальное переключение системных параметров.

Первый итоговый v143 CTest-пакет выполнил 22 теста: 21 PASS и один FAIL в новом
`apptraverse_window_catalog_win32_chrome_adaptive_test`. Проверка уведомлений
системных настроек обнаружила реальную ошибку backend: внутренний
SetScrollInfo вызывал WM_WINDOWPOSCHANGED, который ошибочно ставил изменение
desktop frame в очередь модели. Поэтому refresh не был полностью локальным.
Внутреннее изменение scrollbars отделено от пользовательского изменения frame.
После исправления выполнен затронутый пакет из семи Windows-тестов, все PASS.
Первоначальный FAIL сохраняется как отдельный результат, а не переименовывается
в PASS.

В этом пакете прошли модельный набор WindowCatalog (153.38 s), headless
(11.67 s), USER/supply/history и проверки controls/host/presenters. Реальная
native acceptance выполнялась отдельно и прошла за 15.28 s. Сборка 23 targets,
пакет 22 тестов и отдельная native acceptance — разные объёмы проверки.

Пакет после Windows-only scrollbar-исправления: native acceptance 15.41 s,
chrome adaptive 3.97 s, native button 0.21 s, frame sync 0.77 s, frame publication
0.35 s, application message host 0.06 s, command route 0.74 s; итог 7/7 PASS,
21.74 s. Модель и заголовки после первого успешного 21-тестового результата
не менялись; кроме Windows backend изменён только комментарий builder.
Поэтому итоговый выбранный объём — 23 различных теста PASS совокупно, а не
заявление о едином последнем запуске всех 23 тестов.

Следующая ручная проверка GUI обнаружила отсутствие действия Return у
сфокусированной owner-draw кнопки. Space действительно создал Window 1,
Alt+F4 закрыл дочернее окно, а каталог показал Closed; вертикальная раскладка
применилась через меню. После обнаружения добавлена узкая маршрутизация
Enter → BM_CLICK только для push/default/owner-draw BUTTON. Отдельный регрессионный
message-pump сценарий прошёл: три точных click, неизменное поведение checkbox/radio
и сохранение CRLF в multiline EDIT. Финальный чистый повтор host/native
acceptance после этого исправления прошёл 2/2 за 16.12 s: native 15.65 s,
host keyboard 0.42 s.

Финальная ручная GUI-проверка keyboard-сборки прошла: в свежем Debug Space
создал Window 1; Alt+F4 закрыл дочернее окно, строка стала Closed; меню выбрало
vertical. После корректного закрытия load-only с фактической папкой USER
восстановил Window 1 Closed и vertical layout. Реальные Tab до Add и Return
создали Window 2 после асинхронной публикации. Оба приложения закрыты.
Чистые снимки: `<workspace>/outputs/WindowCatalog/vertical-layout.jpg` и
`<workspace>/outputs/WindowCatalog/restored-layout.jpg`.

Первоначальные попытки повторной линковки и native foreground acceptance
пересеклись с работающим ручным load-only окном: исполняемый файл был занят,
а тест не получил foreground. После закрытия ручных окон сборка и keyboard
host test повторены успешно. Эти попытки относятся к предусловиям harness,
а не к регрессии продукта; итоговый чистый пакет проверяется отдельно.

После этого чистого пакета исходники реализации заморожены; дальнейшие правки
относятся только к отчёту. Все ручные приложения и тестовые процессы закрыты.
Прямая PE-таблица финальных exe проверена повторно: новых обязательных
несовместимых импортов нет. Финальные Debug exe скопированы в
`<workspace>/outputs/WindowCatalog`:

| Файл | SHA-256 |
| --- | --- |
| `win32_window_catalog_demo.exe` | `5D9F6F55037CEDCF7DF9395545CBB9122D0AFACC91CBB957F372A229774FA800` |
| `win32_window_catalog_demo_load_only.exe` | `F991894D8DD0FA8EB2A2D8CC25AE7D2787464B1EE68459DBC4C00A04F5F2DAA9` |

## Не выполнено и что не заявляется

| Область | Статус |
| --- | --- |
| Реальный Windows 7 host/VM | NOT RUN |
| Реальный Narrator/NVDA и полный accessibility-сценарий | NOT RUN |
| Переключение системного High Contrast | NOT RUN |
| Нестандартный системный размер текста | NOT RUN |
| Физические мониторы с разным DPI | NOT RUN |
| Новые native адаптеры Linux/Android/Web/macOS/iOS | PLANNED; пока сохраняют прежнюю раскладку |
| Повторная новая 52-process межплатформенная USER-матрица | NOT RUN |

Исторические PASS предыдущих платформенных работ не переносятся на эту новую
схему. Дерево accessibility и нативные контролы дают основу для доступности,
но не являются свидетельством полной совместимости со скринридером. Проверка
актуальной тёмной схемы не заменяет матрицу light/dark/High Contrast и всех
состояний контролов.

## Воспроизведение

Собирать Debug совместимым v143 toolset, используя уже подготовленные локальные
dependency sources. Не переиспользовать CMakeCache другого checkout и не
подменять его source root. Новые профили задаются через `--state-dir`.

Обычный Debug всегда создаёт свежую вложенную папку
`<requested-state-root>/debug-run-N/user/user.bin`. Для ручного cold restore
load-only binary получает `--state-dir <requested-state-root>/debug-run-N/user`,
то есть фактическую папку с user.bin. Указание внешней wrapper-папки не загружает
этот USER. Первоначальная ручная попытка с wrapper-путём была ошибкой команды,
а не регрессией продукта; финальный повтор с правильным путём прошёл.

После сборки соответствующих targets:

```text
ctest --test-dir <build> --output-on-failure -R "ui_constraint_layout|ui_layout_state|window_catalog_headless|window_catalog_model|window_catalog_win32_chrome_adaptive|window_catalog_main_native_frame_coalesce"
<build>/examples/window_catalog/windows/win32_window_catalog_demo.exe --distill-only <new-output-directory>
dumpbin /imports <build>/examples/window_catalog/windows/win32_window_catalog_demo.exe
```

Native acceptance исполняется в интерактивном desktop с выполненным
foreground-предусловием. Снимки экрана дополняют проверки геометрии и действий,
а не заменяют их. Итоговый источник фиксируется коммитом текущего задания.
