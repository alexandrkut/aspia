Aspia
=====
Remote desktop, file transfer and system information tool.

This fork
--------

This is a fork of Aspia 3.1.1 with two features and a set of changes to the build. The
base is the tag v3.1.1 of the upstream, and everything below is what the branch
`router-2fa` adds to it.

**Connect to a host by its ID.** The client can be given the ID of a host and connect to
it, without the host having to be in the address book of the client first: the router
keeps the records of the hosts it knows, and the client asks the router for the one it
wants. The name of the user and the password can be given on the command line as well,
which is what a script that connects without a person at the keyboard needs.

**The second factor per role.** The router can let a role in with its password alone,
and the three roles are switched separately - an administrator reaches every host and
every setting, while an operator reaches a host and nothing else. All three are on by
default, and every login that went through without a code is written to the log.

**The router without a window.** The router is a daemon and draws nothing, so the build
can leave out QtGui, the widgets, the SVG, the codecs of the picture and the sound and
the X11 and Wayland libraries. On a machine that builds the dependencies from source,
that is the difference between an hour and a day.

**The scripts of the build.** Debian 10 is from 2019 and needs a toolchain that did not
exist then, and the ports of vcpkg stall on mirrors now and then. The scripts that build
the router on Linux and on Windows are in the repository.

The whole of it is described in [MODIFICATIONS.md](MODIFICATIONS.md).

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
