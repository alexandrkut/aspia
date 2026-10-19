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

#include "client/connect_request.h"

#include <optional>

#include <QCoreApplication>
#include <QStringLiteral>

#include "base/crypto/secure_string.h"
#include "client/host_url.h"

namespace {

using namespace Qt::StringLiterals;

struct SessionTypeName
{
    const char* name;
    proto::peer::SessionType type;
};

// Every kind a session may be, by the short name --session-type accepts and by the value behind
// that name. A kind missing from the list cannot be asked for from the command line.
constexpr SessionTypeName kSessionTypes[] = {
    { "desktop", proto::peer::SESSION_TYPE_DESKTOP },
    { "file", proto::peer::SESSION_TYPE_FILE_TRANSFER },
    { "info", proto::peer::SESSION_TYPE_SYSTEM_INFO },
    { "chat", proto::peer::SESSION_TYPE_CHAT },
    { "terminal", proto::peer::SESSION_TYPE_TERMINAL }};

//--------------------------------------------------------------------------------------------------
// A record ID is a positive number and nothing else. The value is not checked against the database
// here: the client may be starting before it was unlocked, and the lookup that opens the session
// reports a record that is not there in a way the user can act on.
std::optional<qint64> entryIdFromString(const QString& str)
{
    bool ok = false;
    const qint64 entry_id = str.trimmed().toLongLong(&ok);

    if (!ok || entry_id <= 0)
        return std::nullopt;

    return entry_id;
}

//--------------------------------------------------------------------------------------------------
// The value of an option, written either "--connect 3" or "--connect=3". |recognized| tells a value
// that was written from an option that is not there at all, which is not a refusal.
struct OptionValue
{
    std::optional<QString> value;
    bool recognized = false;
};

OptionValue optionValue(const QStringList& arguments, int* index, const QString& option,
                         const QString& short_option = QString())
{
    const QString& argument = arguments.at(*index);

    if (argument == option || (!short_option.isEmpty() && argument == short_option))
    {
        // The value is the next argument; there is nothing else on this one.
        if (*index + 1 >= arguments.size())
            return { std::nullopt, true };

        ++(*index);
        return { arguments.at(*index), true };
    }

    if (argument.startsWith(option + u'='))
        return { argument.mid(static_cast<qsizetype>(option.size()) + 1), true };

    return {};
}

} // namespace

//--------------------------------------------------------------------------------------------------
bool ConnectRequest::parse(const QStringList& arguments, ConnectRequest* request, QString* url,
                           QString* error)
{
    ConnectRequest result;
    QString start_url;

    for (int i = 0; i < arguments.size(); ++i)
    {
        const QString& argument = arguments.at(i);

        if (const OptionValue connect = optionValue(arguments, &i, u"--connect"_s, u"-c"_s);
            connect.recognized)
        {
            // An option at the end of the line with nothing behind it is a refusal, not a plain
            // start: something was asked for and could not be read.
            const std::optional<qint64> entry_id =
                connect.value ? entryIdFromString(*connect.value) : std::nullopt;

            if (!entry_id)
            {
                if (error)
                {
                    *error = connect.value ?
                        QCoreApplication::translate(
                            "Client", "\"%1\" is not a valid host ID.").arg(*connect.value) :
                        QCoreApplication::translate(
                            "Client", "The host ID of --connect is missing.");
                }
                return false;
            }

            // Spelled out, so it wins over a bare argument found anywhere on the line.
            result.entry_id_ = *entry_id;
            continue;
        }

        if (const OptionValue session = optionValue(arguments, &i, u"--session-type"_s);
            session.recognized)
        {
            proto::peer::SessionType session_type = session.value ?
                sessionTypeFromName(*session.value) : proto::peer::SESSION_TYPE_UNKNOWN;

            if (session_type == proto::peer::SESSION_TYPE_UNKNOWN)
            {
                if (error)
                {
                    *error = session.value ?
                        QCoreApplication::translate(
                            "Client", "\"%1\" is not a valid session type. Use desktop, file, "
                                       "info, chat or terminal.").arg(*session.value) :
                        QCoreApplication::translate(
                            "Client", "The session type of --session-type is missing.");
                }
                return false;
            }

            result.session_type_ = session_type;
            continue;
        }

        if (const OptionValue username = optionValue(arguments, &i, u"--username"_s, u"-u"_s);
            username.recognized)
        {
            if (!username.value)
            {
                if (error)
                {
                    *error = QCoreApplication::translate(
                        "Client", "The user name of --username is missing.");
                }
                return false;
            }

            result.username_ = *username.value;
            continue;
        }

        if (const OptionValue password = optionValue(arguments, &i, u"--password"_s); password.recognized)
        {
            if (!password.value)
            {
                if (error)
                {
                    *error = QCoreApplication::translate(
                        "Client", "The password of --password is missing.");
                }
                return false;
            }

            result.password_ = SecureString(*password.value);
            continue;
        }

        if (argument.startsWith(u'-'))
        {
            // An option this parser knows nothing about, including the ones of Qt itself
            // (--help, --version). Deciding what to do with those belongs to the caller, which
            // hands over a line its own parser has already accepted.
            continue;
        }

        // What is left is a link or a bare record ID. A link wins: it names a host of its own.
        if (HostUrl::isHostUrl(argument))
        {
            start_url = argument;
            continue;
        }

        std::optional<qint64> entry_id = entryIdFromString(argument);
        if (!entry_id)
        {
            if (error)
            {
                *error = QCoreApplication::translate(
                    "Client", "\"%1\" is neither an aspia:// link nor a host ID.").arg(argument);
            }
            return false;
        }

        if (result.entry_id_ <= 0)
            result.entry_id_ = *entry_id;
    }

    // Half of a pair is no pair: the session would fall back to asking for the rest in a dialog,
    // which is not what a script asked for, and mixing one given value with one stored from the
    // record is not something anybody means on a command line.
    if (result.username_.isEmpty() != result.password_.isEmpty())
    {
        if (error)
        {
            *error = result.username_.isEmpty() ?
                QCoreApplication::translate(
                    "Client", "--username requires --password as well.") :
                QCoreApplication::translate(
                    "Client", "--password requires --username as well.");
        }
        return false;
    }

    // Credentials on their own are a connection to no record at all.
    if (!result.username_.isEmpty() && result.entry_id_ <= 0)
    {
        if (error)
        {
            *error = QCoreApplication::translate(
                "Client", "--username and --password need a host to connect to "
                           "(--connect or an aspia:// link).");
        }
        return false;
    }

    *request = result;

    if (url)
        *url = start_url;

    return true;
}

//--------------------------------------------------------------------------------------------------
proto::peer::SessionType ConnectRequest::sessionTypeFromName(const QString& name)
{
    const QString lower = name.trimmed().toLower();

    for (const auto& entry : kSessionTypes)
    {
        if (lower == QLatin1String(entry.name))
            return entry.type;
    }

    // A number is accepted too, so that a script which already speaks the wire values needs no
    // table. Only the values of the kinds above: a combination of their bits names no session.
    bool number_ok = false;
    const int value = lower.toInt(&number_ok);

    if (number_ok)
    {
        for (const auto& entry : kSessionTypes)
        {
            if (value == static_cast<int>(entry.type))
                return entry.type;
        }
    }

    return proto::peer::SESSION_TYPE_UNKNOWN;
}