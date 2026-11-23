# Сервис жизненного цикла приложений

Один запущенный `update_lab_provider` обслуживает несколько приложений по одному UID Æther. Приложение указывает ID в запросе. У каждого ID свой каталог версий, полные поставки, дельты и пакеты. Профиль устройства и установка также привязаны к ID.

```text
/var/lib/apptraverse-lifecycle-store/
  apps/
    app-lifecycle-demo/
      catalog.bin
      images/R0…R3/
    app-lifecycle-demo-alt/
      catalog.bin
      images/R0…R1/
```

Издатель загружает Windows EXE и launcher через SSH. На сервере `update_lab_provider publish` вычисляет прямые и обратные дельты, сжимает пакеты LZMA1 и атомарно заменяет каталог выбранного приложения. Для клиента Æther каталог становится доступен на следующем запросе. Сервисный процесс и его UID продолжают работать во время публикации. Скрипт создаёт временный каталог для каждой загрузки и удаляет переданные файлы после выхода. Опубликованные компоненты имеют неизменяемые ID. Повторный запуск с теми же бинарниками безопасен, а продление с `-Through 1` до `-Through 3` добавляет следующие версии.

## Публикация с Windows

```powershell
./tools/publish_update_lifecycle.ps1 -Remote USER@HOST -App lifecycle-demo -Through 3
./tools/publish_update_lifecycle.ps1 -Remote USER@HOST -App lifecycle-demo-alt -Through 1 -NoBuild
```

Для первого запуска нужен SSH-доступ к тестовой VM и разрешение выполнять команду издателя через `sudo -n`. Сценарий использует известный ключ хоста. Параметр `-IdentityFile` задаёт путь к SSH-ключу, если ключ отличается от используемого OpenSSH по умолчанию. `-NoBuild` берёт уже собранные EXE, что удобно при публикации нескольких приложений из одной демосборки. Сценарий дистилляции выпускает R0–R3 для любого валидного ID. Переводы и заголовок хранятся в отдельном состоянии каждого приложения.

## Развёртывание на Linux x64

Соберите комплект из каталога репозитория в WSL или Linux. Он включает исполняемый файл поставщика и библиотеки Linux, нужные для запуска на более старой тестовой VM.

```sh
mkdir -p artifacts/update-lifecycle
UPDATE_LAB_PROVIDER_BUNDLE="$PWD/artifacts/update-lifecycle/provider.tar.gz" \
  bash tools/build_update_lab_linux.sh ~/update-lifecycle-build
```

Передайте архив на тестовый Linux-сервер. Для новой установки выполните команды из каталога, куда положили архив.

```sh
id apptraverse-lifecycle >/dev/null 2>&1 ||
  sudo useradd --system --user-group --no-create-home --shell /usr/sbin/nologin apptraverse-lifecycle
sudo install -d -m 0755 /opt/apptraverse-lifecycle
sudo install -d -m 0750 -o apptraverse-lifecycle -g apptraverse-lifecycle /var/lib/apptraverse-lifecycle-store
sudo tar --no-same-owner -xzf provider.tar.gz -C /opt/apptraverse-lifecycle
sudo install -m 0644 /opt/apptraverse-lifecycle/apptraverse-lifecycle.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now apptraverse-lifecycle
sudo journalctl -u apptraverse-lifecycle -n 25
sudo cat /var/lib/apptraverse-lifecycle/endpoint.txt
```

`StateDirectory` хранит постоянную идентичность Æther. Системный аккаунт `apptraverse-lifecycle` владеет каталогом поставок для команды публикации. Запущенный сетевой процесс читает каталог из systemd с `ProtectSystem=strict`. Для связи используется исходящий канал Æther. SSH нужен издателю и администратору.

На тестовой VM `aether-test-server` (Google Cloud, `us-central1-a`) работает сервис `apptraverse-lifecycle`. Его тестовый UID Æther `a1ea11f9-68b5-4524-adbb-34ff05e595cb`.
