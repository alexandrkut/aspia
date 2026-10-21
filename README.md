Aspia
=====
Remote desktop, file transfer and system information tool.

Этот форк
---------

Форк Aspia 3.1.2. Изменения:

1. **Подключение через командную строку.** Клиент запускается с ключом `--connect` и
   подключается к хосту по его ID, даже если этого хоста нет в локальной адресной
   книге, поиск происходит на роутере. Логин, пароль и тип подключения передаются по
   желанию.

2. **Отключение 2FA для каждого типа доступа.** В конфиге роутера на сервере 2FA
   выключается отдельно для администратора, менеджера и оператора. Каждый вход,
   прошедший без кода, попадает в лог.

3. **Несколько мониторов одновременно.** Хост позволяет отдавать весь рабочий стол одним
   пространством (в клиенте одновременно отображается рабочий стол двухмониторной
   машины). Добавлен принудительный запрос полного кадра при смене отображаемого экрана.

4. **Возможность экспорта инсталлера с принудительной перезаписью настроек при
   установке.** Экспортированный инсталлер ставится поверх уже установленного хоста:
   старый хост заменяется новым, без вопроса о починке или удалении, а его настройки и
   пользователи переносятся в установленный хост. Галочка «Перезаписать старые данные»
   переносит настройки даже поверх работающего хоста. Имя экспортированного инсталлера
   несёт полную версию: `aspia-host-3.1.2.12-mod-x86_64.msi`.

5. **Экспорт пакета Quick Support с пользователями хоста.** Хост выгружает себя как
   пакет Quick Support, и в этот пакет можно положить пользователей самого хоста: они
   смогут входить на выгруженный хост со своим логином и паролем, а не только с
   одноразовым паролем. Сами пароли в пакет не кладутся — только соль и проверочное
   значение каждого пользователя. Вход по логину на такой хост есть только в клиенте
   мода: оригинальный клиент отвечает хосту Quick Support только одноразовым паролем.

6. **Отключение автоматического обновления.** Проверка обновлений выключена, потому что
   сервер обновлений оригинала ничего не знает о моде. У хоста галочка проверки есть, но
   снята по умолчанию; у клиента проверки нет вовсе. Если её включить, скачиваются
   оригинальные обновления, а они уносят изменения мода.

7. **Номер сборки в версии.** Окно «О программе» и списки показывают полную версию с
   номером сборки этой линии версий: `3.1.2.12-mod`. Сама версия, которой обмениваются
   программы, осталась прежней: три числа, как у оригинала, поэтому клиент и хост
   оригинала работают с модом.

8. **Пометка мода в имени пакета и в его версии.** Имя файла пакета несёт пометку
   `-mod`, и версия пакета тоже: в списке установленных программ видно «Клиент Aspia
   3.1.2.12-mod», «Хост Aspia 3.1.2.12-mod», «Aspia Router 3.1.2.12-mod». Названия
   программ в списке установленных программ русские у клиента и хоста, английские у
   роутера: у оригинала для роутера нет перевода на русский.

В каком пакете изменение:

| Изменение | Клиент | Хост | Роутер |
|:--|:--:|:--:|:--:|
| 1. Подключение через командную строку | + | | |
| 2. Отключение 2FA для каждого типа доступа | | | + |
| 3. Несколько мониторов одновременно | не обязательно* | + | |
| 4. Экспорт инсталлера с перезаписью настроек | | + | |
| 5. Экспорт пакета Quick Support с пользователями | +** | + | |
| 6. Отключение автоматического обновления | + | + | |
| 7. Номер сборки в версии | + | + | + |
| 8. Пометка мода в имени пакета и в версии | + | + | + |

\* В клиенте изменились только название и значок экрана «Все мониторы», вся работа сделана
в хосте. Можно использовать оригинальный клиент.

\*\* Клиент нужен свой: оригинальный клиент входит на хост Quick Support только по
одноразовому паролю, а с пользователями в пакете нужен вход по логину и паролю.

Где это в интерфейсе:

- Экспорт инсталлера хоста — кнопка «Экспорт» в окне настроек хоста, пункт
  «Инсталлер». Галочка «Перезаписать старые данные при установке» переносит настройки
  и пользователей даже поверх работающего хоста; без неё настройки переносятся только
  при первой установке, а данные уже стоящего хоста остаются нетронутыми.
- Экспорт пакета Quick Support — та же кнопка «Экспорт», пункт «Quick Support».
  Галочка «Экспортировать пользователей этого хоста» кладёт в пакет пользователей
  хоста. Файл пакета — `aspia-quick-support.exe`, он самодостаточен: запускается на
  любой машине и подключается к тому же роутеру.
- Отключение 2FA по ролям — конфиг роутера, секция `[router]`, ключи
  `two_factor_admin`, `two_factor_manager`, `two_factor_operator`. По умолчанию 2FA
  включена для всех трёх ролей.

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
