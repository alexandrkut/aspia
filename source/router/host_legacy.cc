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
#include "router/workers/client_worker.h"

namespace {

// The size of the key the router hands to a host that is not approved yet.
constexpr int kMaxIdRequests = 40;

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
    ++id_request_count_;

    if (id_request_count_ > kMaxIdRequests)
    {
        CLOG(ERROR) << "Too many host id requests in one connection (" << id_request_count_
                     << "); disconnecting";
        emit sig_finished(sessionId());
        return;
    }

    Database& db = database();

    if (!db.isValid())
    {
        CLOG(ERROR) << "Failed to connect to database; disconnecting the host";
        emit sig_finished(sessionId());
        return;
    }

    const QByteArray key = QByteArray::fromStdString(host_id_request.key());
    const QByteArray key_hash = GenericHash::hash(GenericHash::Type::BLAKE2b512, key);

    proto::router::legacy::RouterToPeer message;
    proto::router::legacy::HostIdResponse* host_id_response = message.mutable_host_id_response();

    HostId host_id = kInvalidHostId;
    std::string_view error_code = db.hostId(key_hash, &host_id);

    // The host of the previous version of the program has no hardware id and no approval, so it
    // goes into the database right away.
    if (error_code == proto::router::kErrorNotFound &&
        host_id_request.type() == proto::router::legacy::HostIdRequest::NEW_ID)
    {
        // The hash of the key stands for the hardware id: the host has none, and the id of the
        // record must not change when the host comes back.
        if (db.addHost(key_hash, key_hash))
        {
            error_code = db.hostId(key_hash, &host_id);
        }
        else
        {
            CLOG(ERROR) << "Failed to add host to database";
            error_code = proto::router::kErrorInternalError;
        }
    }

    if (error_code == proto::router::kErrorOk)
    {
        host_id_response->set_error_code(proto::router::legacy::HostIdResponse::SUCCESS);
        host_id_response->set_host_id(host_id);
        host_id_response->set_key(host_id_request.key());

        if (!db.updateHostInfo(host_id, key_hash, computerName(), architecture(),
                               version().toString().toStdString(), osName(), address()))
        {
            CLOG(WARNING) << "Failed to update host info for host_id:" << host_id;
        }

        setHostId(host_id);

        emit sig_hostIdAssigned(host_id);
        emit sig_notifyChanged(ClientWorker::NOTIFY_HOSTS);
    }
    else if (error_code == proto::router::kErrorNotFound)
    {
        host_id_response->set_error_code(proto::router::legacy::HostIdResponse::NO_HOST_FOUND);
    }
    else
    {
        host_id_response->set_error_code(proto::router::legacy::HostIdResponse::UNKNOWN);
    }

    sendMessage(0, serialize(message));
}
