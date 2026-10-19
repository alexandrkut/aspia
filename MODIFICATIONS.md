Modifications
=============

This is a fork of Aspia 3.1.1 with two features and a set of changes to the build. The
base is the tag v3.1.1 of the upstream; everything below is what this branch adds to it.

The two features are in one branch, `router-2fa`:

    fadf56d  Keep the scripts that build the router for Linux
    d9dcd4a  Fix what kept the service and the package from working
    404e2ad  Build the router without the parts that draw
    f4ab7da  Say out that the second factor can be switched off per role
    b26d7da  Let the second factor be switched off per role
    9001215  Connect to a host from the command line
    e5c4558  Take the host ID on the command line, not just the record ID
    bb66cc6  Connect by the ID of a host that lives only in a router
    7504b34  Ask the router for a host that was never fetched here

The branch `router-only` holds the second feature without the first, for the case where
the two should not travel together.


The features
------------

### Connect to a host by its ID

The client can be given the ID of a host and connect to it, without the host having to
be in the address book of the client first. The router keeps the records of the hosts it
knows, and the client asks the router for the one it wants.

    aspia_client --connect <record ID or host ID> [--session-type desktop|file|info|chat|terminal]
                  [--username <name> --password <password>]

The name of the user and the password can be given on the command line instead of the ones
stored in the host, which is what a script that connects without a person at the keyboard
needs. The password is only taken when the name is given with it.

The router side of this already exists upstream: the handler
`source/router/handlers/host_id_handler.cc` answers the request of a client for a host it
knows. What this branch adds is the client that asks, and the two ways of naming the
host - the ID of the record in the address book, and the ID of the host itself.

The files are `source/client/connect_request.{h,cc}` and the parts of the client that
take the new options: `source/client/main.cc`, `source/client/application.{h,cc}` and
`source/client/desktop/`.

### The second factor per role

The router can let a role in with its password alone, and the three roles are switched
separately:

    [router]
    two_factor_admin=1
    two_factor_manager=0
    two_factor_operator=0

A role that is switched off is let in without a code, no secret is enrolled and no token
is issued - a session that never had a second factor must not end up holding a device
token that would look like one it did. Every such login is written to the log of the
router, because an account that quietly stopped asking for a code cannot be told apart
from one that was never asked to:

    WARNING User "admin" ( 2 )logged in without the second factor: it is switched off
            for this role in the configuration of the router

The three are separate because the roles are not equally exposed: an administrator
reaches every host and every setting, while an operator reaches a host and nothing else.
Turning the second factor off for the first is not a reason to turn it off for the third.

All three default to on. A configuration that never mentioned them - or a key that got
lost on the way - keeps the accounts protected rather than silently opening them. A value
that is neither a number nor a word keeps the second factor on as well, and a misspelt
key is not a key at all: the default is the safe one.

The files are `source/router/settings.{h,cc}`, `source/router/handlers/two_factor_handler.{h,cc}`
and the three clients of the router: `source/router/client_admin.{h,cc}`,
`source/router/client_manager.{h,cc}` and `source/router/client_operator.{h,cc}`.


The build
---------

### The router without a window

The router is a daemon. It opens sockets, keeps a database and speaks the protocol, and
nothing in it draws anything - yet the build brought in QtGui, the widgets, the SVG, the
printing support, the codecs of the picture and the sound, the tools that make the
translation files and the X11 and Wayland libraries. On a machine that has to build all
of that from source, that is the difference between an hour and a day.

The lists of the libraries are now split into the ones every part needs and the ones only
the parts with a window need, and the option `ASPIA_ROUTER_ONLY` builds the router alone:

    cmake --preset local-linux-x64-router
    cmake --build --preset local-linux-x64-router --config Release --target aspia_router

The manifest of the packages the router needs lives in `router-manifest/` and names a
qtbase without the gui, the widgets, the sql and the test library. The dependencies of
the full build and of the router alone do not fight over one tree, because the preset
gives each of them an installed directory of its own.

The port of qtbase lives in another repository, and a change to a port of it cannot be
committed there from here. The patch that lets the gui feature be switched off is kept in
`patches/` instead, and `installer/linux/build/build-patches.sh` puts it on the port.

### The scripts of the build

`installer/linux/build/` holds the scripts that build the router on Debian 10 and on
anything close to it. The distribution is from 2019, and the project needs things that did
not exist then: a compiler with C++20 for Qt 6.11, a CMake that understands the presets
of the project, a ninja that CMake can talk to, an Autoconf that gperf accepts and a
Python that systemd accepts. The scripts build them from source, against the headers of
the system, so that what comes out still speaks the glibc of that system.

The README beside them tells the order and the reason of each piece.

### The scripts of the build on Windows

`installer/windows/build/` holds the scripts that build Aspia on Windows the way the CI
does, and that fetch what the build needs before it starts. They are the counterpart of
the scripts for Linux, and they were written for this fork: vcpkg downloads the sources
of its ports one at a time, and the mirrors it uses stall now and then - a connection is
accepted and the body never comes, and vcpkg waits instead of failing over.

`fetch-sources.ps1` fetches the sources with several threads at once and checks them
against the hashes the ports declare, which is what makes the three hundred megabytes of
the Qt sources take minutes instead of an hour. `build-supervisor.ps1` watches a build
that is already running and deals with such a stall when it sees one, and
`build-status.ps1` is a snapshot of the build.


The fixes
---------

### The service and the signals

The signal handlers for SIGKILL and SIGSTOP were installed anyway. The kernel does not
let a process catch those two, so the call failed every time the service started and
stopped, and the two lines in the log looked like a failure of the service. The other
five are caught as before.

### The headers of DRM

The headers of DRM are in a directory of their own, and the name of that directory is not
the same everywhere: some distributions put them in `drm/`, others in `libdrm/`. The file
that reads what is known about the display of the machine now asks for both, and a
machine that has neither gets no information about the driver of the card, which is the
one thing read through those headers.

### The package

The router links against libsystemd, and the SONAME of it was not in the list of the
packages the dependencies are named after, so the build of the package stopped with "no
Debian package known for libsystemd.so.0".

The version of the binary is also read differently now. The version of a build that is
not the released one carries a suffix, and the file of the sources may have the carriage
returns of a checkout on Windows in it, and both made the check for the version fail on a
binary that was right.

### The presets

`CMakePresets.json` had no build presets at all, so `cmake --build --preset` could not
work for any of them.
