//
// Aspia Project
// Copyright (C) 2016-2026 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#ifndef ROUTER_SETTINGS_H
#define ROUTER_SETTINGS_H

#include <QByteArray>
#include <QStringList>

#include "base/ini_file.h"

class SecureByteArray;

class Settings
{
public:
    Settings();
    ~Settings();

    QString filePath();

    bool isEmpty() const;
    bool hasError() const;
    void reset();
    bool sync();

    void setHostPort(quint16 port);
    quint16 hostPort() const;

    void setClientPort(quint16 port);
    quint16 clientPort() const;

    void setRelayPort(quint16 port);
    quint16 relayPort() const;

    // The address a listener binds to. Empty means every interface.
    void setClientListenInterface(const QString& iface);
    QString clientListenInterface() const;

    void setHostListenInterface(const QString& iface);
    QString hostListenInterface() const;

    void setRelayListenInterface(const QString& iface);
    QString relayListenInterface() const;

    void setStunListenInterface(const QString& iface);
    QString stunListenInterface() const;

    void setHostPrivateKey(const SecureByteArray& private_key);
    SecureByteArray hostPrivateKey() const;

    void setRelayPrivateKey(const SecureByteArray& private_key);
    SecureByteArray relayPrivateKey() const;

    using WhiteList = QStringList;

    void setClientWhiteList(const WhiteList& list);
    WhiteList clientWhiteList() const;

    void setHostWhiteList(const WhiteList& list);
    WhiteList hostWhiteList() const;

    void setRelayWhiteList(const WhiteList& list);
    WhiteList relayWhiteList() const;

    void setSeedKey(const QByteArray& seed_key);
    QByteArray seedKey() const;

    void setEnableStun(bool enable);
    bool isStunEnabled() const;

    void setStunPort(quint16 port);
    quint16 stunPort() const;

    // Whether a user has to answer the second factor on top of the password, per role. A role
    // that is switched off is let in with its password alone, which is a deliberate weakening of
    // the account and is written to the log of every such login.
    //
    // The three are separate because the roles are not equally exposed: an administrator reaches
    // every host and every setting, while an operator reaches a host and nothing else. Turning
    // the second factor off for the first is not a reason to turn it off for the third.
    //
    // All three default to true: the second factor is on until the file says otherwise, so a
    // configuration that never mentioned them - or a key that got lost on the way - keeps the
    // accounts protected rather than silently opening them.
    void setTwoFactorForAdmin(bool enabled);
    bool isTwoFactorForAdminEnabled() const;

    void setTwoFactorForManager(bool enabled);
    bool isTwoFactorForManagerEnabled() const;

    void setTwoFactorForOperator(bool enabled);
    bool isTwoFactorForOperatorEnabled() const;

    // The value for one role, given as the proto::router::SessionType that carries it. A role
    // this build does not know is answered as protected: refusing to check the second factor of
    // a role nobody thought about is exactly the mistake these settings exist to prevent.
    bool isTwoFactorEnabledFor(quint32 session_type) const;

private:
    void setListenInterface(const QByteArray& section, const QString& iface);
    QString listenInterface(const QByteArray& section) const;

    void setWhiteList(const QByteArray& section, const WhiteList& value);
    WhiteList whiteList(const QByteArray& section) const;

    IniFile ini_;
};

#endif // ROUTER_SETTINGS_H
