Aspia
=====
Remote desktop, file transfer and system information tool.

Этот форк
---------

Форк Aspia 3.1.2.

Хосты версии 2.7 подключаются только через роутер и relay линейки 3.0
(`Aspia Router 3.0.37.2-mod`): роутер версии 3.1.* такие хосты не принимает, relay
версии 3.1.* их не понимает.

Изменения:

1. **Подключение через командную строку.**  
   Добавлен ключ `--connect`, в который передается либо ID, либо IP, либо hostname. Логин,
   пароль и тип подключения передаются опционально.

2. **Отключение 2FA для каждого типа доступа.**  
   В конфиге роутера на сервере 2FA выключается отдельно для администратора, менеджера и
   оператора:

       [router]
       two_factor_admin=1
       two_factor_manager=0
       two_factor_operator=0

   Роль с выключенным переключателем входит без кода, и каждый такой вход пишется в лог
   роутера. По умолчанию 2FA включена для всех трёх ролей.

3. **Несколько мониторов одновременно.**  
   Хост снимает весь виртуальный рабочий стол одним кадром и отдаёт его как отдельный
   экран «Все мониторы» рядом с мониторами. Клиент показывает его со значком двух
   мониторов. Добавлен принудительный запрос полного кадра при смене отображаемого
   экрана.

4. **Возможность экспорта инсталлера хоста с принудительной перезаписью настроек при установке.**  
   При экспорте инсталлера можно выбрать опцию «Затирать старые данные при установке». При
   установке этого пакета настройки хоста будут перезаписаны на данные из инсталлера (адрес
   роутера, пользователи, пароль конфигурации и так далее), без этой галочки данные
   сохраняются.

5. **Возможность экспорта QuickSupport со встроенным пользователем**  
   При экспорте QuickSupport есть возможность выбрать опцию «Выгружать пользователей этого
   хоста». Они смогут входить на QuickSupport своим логином и паролем, а не только с
   одноразовым паролем. Сами пароли в пакет не кладутся — только соль и проверочное
   значение каждого пользователя. Вход по логину на такой хост есть только в клиенте
   мода: оригинальный клиент отвечает хосту Quick Support только одноразовым паролем.

6. **Автоматическое обновление отключено.**  
   Проверку можно включить, но при появлении новой версии скачаются оригинальные пакеты:
   сервер обновлений оригинала ничего не знает о моде.
   **Сервер обновления мода будет запущен позже.**

7. **Поддержка хостов версии 2.7.**  
   Клиент мода умеет подключаться к хостам Aspia 2.7: если хост не понимает рукопожатие
   текущей версии протокола, клиент сам переходит на прежний протокол — свой формат
   кадров, своё аутентифицирование и свой алгоритм шифрования. Поддерживается и прямое
   подключение (IP или имя), и подключение по ID через роутер; ключи те же, что и для
   современных хостов (`--connect`, `--username`, `--password`). Чего в старом протоколе
   нет, при подключении отключается: чат, терминал, сведения о системе, передача файлов
   и диспетчер задач.
   Для подключения к хосту 2.7 через роутер нужен роутер линейки 3.0 — Aspia Router
   3.0.37.2-mod — и relay линейки 3.0: relay версии 3.1.* старых пиров не понимает.
   Роутер версии 3.1.* хосты 2.7 не принимает.

**В каком пакете изменение:**

| Изменение | Клиент | Хост | Роутер |
|:--|:--:|:--:|:--:|
| 1. Подключение через командную строку | + | | |
| 2. Отключение 2FA для каждого типа доступа | | | + |
| 3. Несколько мониторов одновременно | не обязательно* | + | |
| 4. Экспорт инсталлера хоста с перезаписью настроек | | + | |
| 5. Возможность экспорта QuickSupport со встроенным пользователем | +** | + | |
| 6. Отключение автоматического обновления | + | + | |
| 7. Поддержка хостов версии 2.7 (с роутером и релеем 3.0) | + | | |

\* В клиенте изменились только название и значок экрана «Все мониторы», вся работа сделана в хосте. Можно использовать оригинальный клиент.

\*\* Клиент нужен свой: оригинальный клиент входит на хост Quick Support только по одноразовому паролю, а с пользователями в пакете нужен вход по логину и паролю.

Где это в интерфейсе:

- Подключение из командной строки — ключ `--connect` (он же `-c`) с ID записи адресной
  книги, с ID хоста или с его адресом. Логин с паролем для этой сессии задаются ключами
  `--username` и `--password`, вид сессии — ключом `--session-type` (`desktop`, `file`,
  `info`, `chat`, `terminal`). Хост, названный по IP или по имени, подключается напрямую,
  без роутера: он должен быть в локальной сети или иметь проброшенный порт. Записи такого
  хоста в адресной книге нет, и взять логин с паролем неоткуда — их надо задать ключами.
- Экспорт инсталлера хоста — кнопка «Экспорт» в окне настроек хоста, пункт
  «Установщик». Галочка «Затирать старые данные при установке» перезаписывает настройки
  и пользователей данными из инсталлера даже поверх работающего хоста; без неё настройки
  переносятся только при первой установке, а данные уже стоящего хоста остаются
  нетронутыми.
- Экспорт пакета Quick Support — та же кнопка «Экспорт», пункт «Быстрая поддержка».
  Галочка «Выгружать пользователей этого хоста» кладёт в пакет пользователей хоста. Файл
  пакета — `aspia-quick-support.exe`, он самодостаточен: запускается на любой машине и
  подключается к тому же роутеру.
- Отключение 2FA по ролям — конфиг роутера, секция `[router]`, ключи
  `two_factor_admin`, `two_factor_manager`, `two_factor_operator`. По умолчанию 2FA
  включена для всех трёх ролей.

- Поддержка хостов версии 2.7 — ничего настраивать не надо: клиент сам узнаёт старый хост
  по рукопожатию и переходит на прежний протокол. Хост 2.7 подключается как обычный:
  напрямую по IP или по имени, либо по ID через роутер линейки 3.0 (`Aspia Router
  3.0.37.2-mod`) и relay линейки 3.0; роутер версии 3.1.* хосты 2.7 не принимает.

Обновление
----------

Автоматическое обновление отключено. При включении скачиваются оригинальные обновления.

With Aspia, you can create your own NAT traversal infrastructure (using Router and Relay servers) with connection by ID or use direct connections. Aspia supports many features. Among them, detailed information about the system, terminal, audio, text chat.

|Build Status|
|:--:|
|[![Build status](https://github.com/dchapyshev/aspia/actions/workflows/windows.yml/badge.svg?branch=master)](https://github.com/dchapyshev/aspia/actions/workflows/windows.yml) [![Build status](https://github.com/dchapyshev/aspia/actions/workflows/linux.yml/badge.svg?branch=master)](https://github.com/dchapyshev/aspia/actions/workflows/linux.yml) [![Build status](https://github.com/dchapyshev/aspia/actions/workflows/macos.yml/badge.svg?branch=master)](https://github.com/dchapyshev/aspia/actions/workflows/macos.yml) [![Build status](https://github.com/dchapyshev/aspia/actions/workflows/android.yml/badge.svg?branch=master)](https://github.com/dchapyshev/aspia/actions/workflows/android.yml)|

Currently supported
-------------------
- Remote desktop management
- File transfer
- Remote terminal
- System information
- Text chat
- Task manager
- Encryption
- Authorization (it is possible to add users with different access rights)
- Address book with encryption and master-password
- <b>NAT traversal with connection by ID</b> (with using Aspia Router and Aspia Relay)
- Direct connections
- Audio support
- Video recording
- Host and Client for Windows, MacOSX, Linux and Android
- Router/Relay for Windows and Linux
- And much more

System requirements
-------------------
- Windows 7/2008 R2 or higher (x86 or x86_64 CPU)
- Ubuntu 22.04 / RHEL 8.10 (x86_64 CPU)
- MacOSX (x86_64 or ARM64 CPU)
- Android 12+

Contacts
--------
E-Mail: dmitry@aspia.ru

Group in Telegram: [@aspia_talks](https://t.me/aspia_talks)

News in Telegram: [@aspia_news](https://t.me/aspia_news)

Licensing
---------
Project code is available under the GNU General Public License 3.

For more information, see [license agreement](LICENSE.md).

See also
--------
- [Documentation](https://aspia.org/documentation.html)
- [Instructions for building the project (Windows)](https://aspia.org/docs/building-windows)
- [Instructions for building the project (Linux)](https://aspia.org/docs/building-linux)
- [Instructions for building the project (MacOSX)](https://aspia.org/docs/building-macos)
- [Instructions for translators](https://aspia.org/docs/translators)
- [Code of conduct](CODE_OF_CONDUCT.md)
