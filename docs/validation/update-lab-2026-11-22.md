# UpdateLab — выполненные проверки, 2026-11-22

Проверялся отдельный opt-in `examples/update_lab` поверх AppTraverse main
`4bd5f1d9`. Обычный WindowCatalog и его Debug/S0-профили не изменялись.
Это функциональная проверка данного примера, не полный аудит Canon или всех
платформ AppTraverse. Инструкция: [UpdateLab](../update_lab.md).

## Среда и сборка

- Windows x64, Visual Studio 2022/v143, MSVC 19.44, Debug.
- Ubuntu в WSL2, clang 18, GTK 3, Debug. Нативное GTK-окно проверено с
  `GDK_BACKEND=x11 xvfb-run`; это настоящий Linux ELF/GTK, не Wine.
- SDK `4163a08ebf8a841bbb4f1f21319143e792b1399a`, отдельная принадлежащая сборке
  копия и существующий no-exceptions patch v2. Исходный SDK checkout не менялся.
- TCP cloud route, отдельные идентичности поставщика компонентов и загрузчика.
  UpdateLab использует свою конфигурацию, не меняет UDP-конфигурацию чата.
- zstd 1.5.7, application checksum/signature отсутствуют. Контрольная сумма
  Zstandard frame отключена. Байты при проверке дельт сравниваются непосредственно.

На обеих платформах собраны `update_lab_pack`, `update_lab_provider`,
`update_lab_launcher`, `update_lab_e0/e1/e2`, `update_lab_state_test`.
Оба build-script выполнены целиком: incremental build, state-test и публикация;
Linux дополнительно создаёт копии ELF без debug-секций. Windows-скрипт с
`-LinuxBinaries` прочитал ELF из WSL UNC и опубликовал общий каталог из 23
компонентов для двух ОС и четырёх поставок. Содержимое каталога запуска по
умолчанию побайтно совпало с каталогом, использованным в приёмочных тестах.

Проверка выявила две проблемы CMake: CRLF в patch-контексте общей Windows/WSL
папки и косвенную зависимость runtime от telemetry include при
`AE_OBJECTS_RAW_TELE=OFF`. Применение patch теперь допускает различие пробелов
контекста; runtime явно линкует уже используемую библиотеку telemetry.
Сборки после исправлений прошли. Это не изменение сетевого протокола SDK.

## Native-состояния и упаковщик — PASS на Windows и Linux

`update_lab_state_test` проверяет:

- Полный native-граф с объектами локализаций; R1 действительно меняет перевод,
  R2 действительно содержит новый объект Deutsch.
- Точное равенство native-образа результату последовательного применения
  R0→R1→R2→R3 дельт.
- Удаления объектов, классов и S-слоёв, tombstones, пустые карты,
  замену tombstone обычным объектом и сохранение scope metadata.
- Отклонение дельты с другим исходным R без изменения активного состояния.
- Загрузку нового S1 через native origin старого клона, сохранение ObjId и
  пользовательского S0; унаследованный S1 не закрепляется в USER.
- Сборку native транспортного Frame из переставленных и повторных частей,
  включая перекрывающиеся части с изменившимся размером разбиения.

Отдельный Windows CLI-проход `full`, `delta`, `apply`, `inspect` с относительными
именами выходных файлов прошёл; полученный R1 побайтно равен опубликованному
native R1. Короткие имена без родительского каталога поддерживаются.

## Нативное окно — PASS на Windows и Linux

`--native-smoke` подаёт команды через Win32 controls/WM_COMMAND и GTK signals,
затем через ModelObjectProxy, модельные Events, публикацию и отдельное UI-зеркало.
Проверены настоящий текст native label, выбранная строка native language listbox,
создание третьего клона и локальный лимит скорости 65 536 B/s. В конце стоит
Русский/R0, состояние `activated`, и процесс сам закрывает окно.

Финальные маркеры:

- `artifacts/update-lab/native-win-final/native-smoke.ok`.
- `artifacts/update-lab/native-linux-x11-final/native-smoke.ok`.

Дополнительный GTK-проход с автоматическим выбором backend также завершился,
но выдавал GDK seat warnings в WSLg. Финальное свидетельство выше использует
явный X11/Xvfb. Ручной desktop mouse/keyboard acceptance этим не заявляется.

## Процессная матрица — 31/31 PASS

```powershell
python tools/test_update_lab.py --windows-bin build/win-update-lab/examples/update_lab/Debug --linux-bin /home/nickc/update-lab-build/examples/update_lab --store artifacts/update-lab/store-native-final --run artifacts/update-lab/process-native-final
```

Данные: `artifacts/update-lab/process-native-final/results.json` и отдельные
snapshot/log каждого запуска. Файловый источник здесь выбран явно.

Выполнены холодное восстановление USER/LOCAL, замена перевода, добавление языка,
получение соседней native-дельты, принудительный kill посреди `.part` и докачка.
До готовности пакета сохраняются старые R/язык; после готовности меняются вместе.

Проверены настоящий E0→E1 процесс, S1 у старых и новых prefab-клонов, неизменные
ObjId, S0 override, пользовательский S1 override и его сохранение старым E0
при ходе R3→R0→R3. Обновления не подменяют пользовательские клоны новыми.

Настоящий E2 завершился до health acknowledgment; launcher восстановил USER/LOCAL
и запустил E1. Отдельно прошёл таймаут health. Ещё один launcher был принудительно
убит во время живого неподтверждённого E1: lock удерживал профиль до выхода
дочернего процесса, следующий запуск откатил trial и восстановил прежнее состояние.

Ошибка недоступного источника стала persisted DownloadNode/error Event,
сохранила прежний язык; после рестарта с рабочим источником намерение завершилось.

Экспорт USER — один сжатый `.ats`. Windows→Linux импорт сохранил ObjId, заметку,
язык, R и S1 overrides, загрузил именно Linux E1, сохранил Linux device/rate.
Обратный Linux→Windows перенос сохранил те же ObjId и Windows LOCAL, передав
изменённую на Linux заметку. Installations/SDK identities/cache не переносились.

## Реальный клиент Æther — 4/4 PASS

```powershell
python tools/test_update_lab_cloud.py --bin build/win-update-lab/examples/update_lab/Debug --provider CLIENT_UID --run artifacts/update-lab/cloud-native-final
```

Отдельный `update_lab_provider` обслуживал `store-native-final`; файловый источник
не передавался потребителю. Обе стороны — настоящие SDK-клиенты через cloud route.
Итоги: `artifacts/update-lab/cloud-native-final/results.json`; запросы/блоки
зафиксированы в `artifacts/update-lab/provider-native-final/provider.log`.

1. Процесс загрузчика действительно убит после первого принятого блока Deutsch;
   наблюдаемая публикация всё ещё R0/English.
2. Новый процесс восстановил DownloadNode и `.part`, докачал оставшиеся блоки,
   одним коммитом включил R2/Deutsch, сохранил переносимую заметку.
3. Через тот же клиент получены R3, его язык и реальный Windows E1-компонент
   размером 2 074 655 B. Launcher запустил E1, получил health confirmation,
   старые клоны показали S1 префаба.
4. Через того же поставщика скачан настоящий E2-компонент 12 001 B. E2 упал до
   подтверждения; launcher восстановил E1 и пользовательское состояние.

Cached startup кандидата не создаёт SDK-клиент без необходимости: полный набор
уже готов до trial, поэтому health не зависит от второй регистрации/загрузки.
Повторы частей SDK собираются по offset/request ID; прикладных checksum нет.

Дополнительно поставщик был остановлен и запущен с прежним SDK-профилем:
сохранён тот же UID. После этого отдельный native Linux-процесс через своего
настоящего SDK-клиента скачал русский пакет 393 671 B от Windows-поставщика и
атомарно включил его. `cloud-linux-native-final.json` содержит `activated/ru`,
сохранённую заметку и Linux device. Логи лежат в `artifacts/update-lab/` рядом
с ним и в `provider-native-final/provider-restart.log`.

## Компрессия

[Методика, численные результаты и выбор кодека](../update_lab_compression.md).
Канонические полные образы включают локализации. Демонстрационные 384 KiB
языкового delivery-конверта не входят в native corpus и не улучшают статистику.

## Удаление файловых сигнатур

После уточнения запрета файловых сигнатур удалено поле/значение `ATSTATE1`;
кодек переключён на Zstd magicless, без стандартного magic и checksum.
Метаданные пакета занимают 24 B. `state_test` проверяет отсутствие magic-поля,
magicless frame header, отключённый checksum и прежние full/delta/S1 сценарии.
Windows/Linux incremental Debug builds и эти state-tests PASS; native controls
smoke PASS на Windows и GTK/X11. Новая процессная матрица PASS 31/31:
`artifacts/update-lab/process-no-signature/results.json`. В том числе повторены
USER round-trip, partial resume, native E0/E1, E2 rollback и interrupted trial.
Новые полные образы, delta, язык и EXE использовались из `store-no-signature`.
Прежние профили и исторические свидетельства сохранены; старый экспериментальный
delivery format не загружается автоматически. Native USER/LOCAL format не менялся.

Дополнительно новый magicless языковой пакет прошёл настоящую cloud-доставку
через отдельного Æther-поставщика: `cloud-no-signature.json` содержит
`activated/ru`, размер компонента 393 639 B, исходный English оставался активным
до `ready`. Логи: `cloud-no-signature.log`, `provider-no-signature/provider.log`.

## Границы

Физический Linux-компьютер, фактическая Windows 7, Release, Apple/Android/Web,
OS-image upgrades и полная accessibility/DPI-матрица не запускались.
Прототип предполагает корректную доставку. Повреждение пакетов, проверка
подлинности, криптографические digest и production hardening не добавлялись.
Номера и ID опубликованных компонентов считаются неизменяемыми; пересборка
fixture под тем же ID требует нового тестового store/профиля.
