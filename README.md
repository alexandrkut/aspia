Aspia
=====
Remote desktop, file transfer and system information tool.

Этот форк
---------

Это форк Aspia 3.1.1 с двумя возможностями и набором изменений в сборке. Базой является
тег v3.1.1 апстрима, и всё, что описано ниже, — то, что ветка `router-2fa` добавляет к
нему.

**Подключение к хосту по его ID.** Клиенту можно дать ID хоста и подключиться к нему,
хотя бы этого хоста ещё нет в адресной книге клиента: роутер хранит записи тех хостов,
которых знает, и клиент просит у него нужную. Имя пользователя и пароль можно передать
и в командной строке — это то, что нужно скрипту, который подключается без человека за
клавиатурой.

**Второй фактор по ролям.** Роутер может впустить роль только по паролю, и три роли
переключаются по отдельности: администратор достаёт до всех хостов и всех настроек, а
оператор — только до одного хоста и больше ни до чего. По умолчанию включены все три, и
каждый вход, прошедший без кода, пишется в лог.

**Роутер без окна.** Роутер — это демон, и он ничего не рисует, поэтому сборка может
оставить без QtGui, виджетов, SVG, кодеков картинки и звука, библиотек X11 и Wayland. На
машине, которая собирает зависимости из исходников, это разница между часом и сутками.

**Скрипты сборки.** Debian 10 — 2019 года, и ему нужен тулчейн, которого тогда не было,
а порты vcpkg время от времени зависают на зеркалах. Скрипты сборки роутера на Linux и
на Windows лежат в репозитории.

Всё это описано подробно в [MODIFICATIONS.md](MODIFICATIONS.md).

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
