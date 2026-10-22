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

#include "router/host_legacy.h"

#include "base/serialization.h"
#include "base/crypto/generic_hash.h"
#include "base/crypto/random.h"
#include "proto/router_constants.h"
#include "proto/router_host.h"
#include "proto/router_legacy_host.h"
#include "proto/router_peer.h"
#include "router/database.h"
#include "router/handlers/host_id_handler.h"

namespace {

// The error code of the previous version of the program is an enumeration, the current one is a
// string.
proto::router::legacy::HostIdResponse::ErrorCode legacyErrorCode(const std::string& error_code)
{
    if (error_code == proto::router::kErrorOk)
        return proto::router::legacy::HostIdResponse::SUCCESS;

    if (error_code == proto::router::kErrorNotFound)
        return proto::router::legacy::HostIdResponse::NO_HOST_FOUND;

    return proto::router::legacy::HostIdResponse::UNKNOWN;
}

} // namespace

//--------------------------------------------------------------------------------------------------
HostLegacy::HostLegacy(Database& database, TcpChannel* channel, QObject* parent)
    : Host(database, channel, parent)
{
    CLOG(INFO) << "Ctor";
}

//--------------------------------------------------------------------------------------------------
HostLegacy::~HostLegacy()
{
    CLOG(INFO) << "Dtor";
}

//--------------------------------------------------------------------------------------------------
void HostLegacy::onSessionMessage(quint8 /* channel_id */, const QByteArray& buffer)
{
    proto::router::legacy::PeerToRouter message;
    if (!parse(buffer, &message))
    {
        CLOG(ERROR) << "Could not read message from host";
        return;
    }

    if (message.has_host_id_request())
    {
        readHostIdRequest(message.host_id_request());
    }
    else
    {
        CLOG(ERROR) << "Unhandled message from host";
    }
}

//--------------------------------------------------------------------------------------------------
void HostLegacy::sendConnectionOfferMessage(const proto::router::ConnectionOffer& offer)
{
    proto::router::legacy::RouterToPeer message;
    proto::router::legacy::ConnectionOffer* legacy_offer = message.mutable_connection_offer();

    legacy_offer->set_peer_role(proto::router::legacy::ConnectionOffer::HOST);

    if (offer.error_code() == proto::router::kErrorOk)
    {
        legacy_offer->set_error_code(proto::router::legacy::ConnectionOffer::SUCCESS);
    }
    else if (offer.error_code() == proto::router::kErrorHostOffline)
    {
        legacy_offer->set_error_code(proto::router::legacy::ConnectionOffer::PEER_NOT_FOUND);
    }
    else if (offer.error_code() == proto::router::kErrorAccessDenied)
    {
        legacy_offer->set_error_code(proto::router::legacy::ConnectionOffer::ACCESS_DENIED);
    }
    else if (offer.error_code() == proto::router::kErrorKeyPoolEmpty)
    {
        legacy_offer->set_error_code(proto::router::legacy::ConnectionOffer::KEY_POOL_EMPTY);
    }
    else
    {
        legacy_offer->set_error_code(proto::router::legacy::ConnectionOffer::UNKNOWN_ERROR);
    }

    legacy_offer->mutable_relay()->CopyFrom(offer.relay());

    sendMessage(0, serialize(message));
}

//--------------------------------------------------------------------------------------------------
void HostLegacy::readHostIdRequest(const proto::router::legacy::HostIdRequest& host_id_request)
{
    HostIdPeer peer;
    peer.computer_name = computerName();
    peer.architecture = architecture();
    peer.version = version().toString().toStdString();
    peer.os_name = osName();
    peer.address = address();

    ++id_request_count_;

    // The request of the previous version of the program has the same fields as the request of the
    // current version, so the same handler decides what to answer.
    proto::router::HostIdRequest request;
    request.set_type(static_cast<proto::router::HostIdRequest::Type>(host_id_request.type()));
    request.set_key(host_id_request.key());

    const HostIdResult result =
        handleHostIdRequest(database(), request, peer, hostId(), static_cast<int>(id_request_count_));

    if (result.action == HostIdResult::Action::SKIP)
        return;

    if (result.action == HostIdResult::Action::CLOSE)
    {
        emit sig_finished(sessionId());
        return;
    }

    proto::router::legacy::RouterToPeer message;
    proto::router::legacy::HostIdResponse* host_id_response = message.mutable_host_id_response();

    if (result.action == HostIdResult::Action::ISSUE_TEMP_ID)
    {
        // The key and the temporary id belong to this connection. The key is random material
        // handed to the host, and the id is reserved until the connection ends.
        std::string key = Random::string(kHostKeySize);
        key_hash_ = GenericHash::hash(GenericHash::Type::BLAKE2b512, key);
        setHostId(reserveTempHostId());

        host_id_response->set_error_code(proto::router::legacy::HostIdResponse::SUCCESS);
        host_id_response->set_host_id(hostId());
        host_id_response->set_key(std::move(key));

        if (!result.removal_pending)
        {
            emit sig_hostIdAssigned(hostId());
            emit sig_notifyChanged(result.notify_flags);
        }

        sendMessage(0, serialize(message));
        return;
    }

    setHostId(result.host_id);
    telemetry_ = result.telemetry;
    host_id_response->set_error_code(legacyErrorCode(result.error_code));

    if (hostId() != kInvalidHostId)
        host_id_response->set_host_id(hostId());

    sendMessage(0, serialize(message));

    if (result.removal_pending)
        sendRemoveCommand();

    if (hostId() != kInvalidHostId)
    {
        emit sig_hostIdAssigned(hostId());

        if (!result.removal_pending)
            emit sig_notifyChanged(result.notify_flags);
    }
}
