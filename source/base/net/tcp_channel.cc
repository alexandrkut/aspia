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

#include "base/net/tcp_channel.h"

#include <asio/error.hpp>

#include "base/location.h"
#include "base/crypto/large_number_increment.h"
#include "base/crypto/stream_decryptor.h"
#include "base/crypto/stream_encryptor.h"
#include "base/peer/authenticator.h"
#include "proto/key_exchange.h"

namespace {

const qint64 kWriteQueueReservedSize = 128;
const qint64 kWritePoolReservedSize = 32;
const Seconds kKeepAliveInterval { 30 };
const Seconds kKeepAliveTimeout { 30 };

volatile auto g_errorCodeType = qRegisterMetaType<TcpChannel::ErrorCode>();

//--------------------------------------------------------------------------------------------------
int calculateSpeed(int last_speed, MilliSeconds duration, qint64 bytes)
{
    static const double kAlpha = 0.1;
    const qint64 ms = duration.count();
    if (ms <= 0)
        return last_speed;
    return int((kAlpha * ((1000.0 / double(ms)) * double(bytes))) + ((1.0 - kAlpha) * double(last_speed)));
}

//--------------------------------------------------------------------------------------------------
quint32 makeInstanceId()
{
    static thread_local quint32 instance_id = 0;
    ++instance_id;
    return instance_id;
}

//--------------------------------------------------------------------------------------------------
void resizeBuffer(QByteArray* buffer, qint64 size)
{
    if (size < 0)
        size = 0;

    if (buffer->capacity() < size)
    {
        buffer->clear();
        buffer->reserve(size);
    }

    buffer->resize(size);
}

} // namespace

//--------------------------------------------------------------------------------------------------
const quint32 TcpChannel::kMaxMessageSize = 5 * 1024 * 1024; // 5 MB
const quint32 TcpChannel::kMaxAuthMessageSize = 16 * 1024; // 16 KB

//--------------------------------------------------------------------------------------------------
TcpChannel::TcpChannel(Type type, Authenticator* authenticator, QObject* parent, Protocol protocol)
    : QObject(parent),
      instance_id_(makeInstanceId()),
      type_(type),
      protocol_(protocol),
      authenticator_(authenticator)
{
    init();
}

//--------------------------------------------------------------------------------------------------
TcpChannel::~TcpChannel()
{
    // Mark guard so that any pending ASIO handlers (already completed but not yet dispatched)
    // will see the object is gone.
    io_->alive = false;
}

//--------------------------------------------------------------------------------------------------
bool TcpChannel::VariableSizeReader::addByte(quint8 byte)
{
    DCHECK_LT(pos_, sizeof(buffer_));

    buffer_[pos_++] = byte;

    // The highest bit of the byte means that the number continues in the next byte. The number is
    // at most four bytes long.
    return (pos_ == sizeof(buffer_)) || !(byte & 0x80);
}

//--------------------------------------------------------------------------------------------------
size_t TcpChannel::VariableSizeReader::messageSize() const
{
    size_t result = buffer_[0] & 0x7F;

    if (pos_ >= 2)
        result += static_cast<size_t>(buffer_[1] & 0x7F) << 7;

    if (pos_ >= 3)
        result += static_cast<size_t>(buffer_[2] & 0x7F) << 14;

    if (pos_ >= 4)
        result += static_cast<size_t>(buffer_[3]) << 21;

    return result;
}

//--------------------------------------------------------------------------------------------------
const QByteArray& TcpChannel::VariableSizeWriter::variableSize(size_t size)
{
    quint8 bytes[4] = { 0 };
    size_t length = 1;

    bytes[0] = static_cast<quint8>(size & 0x7F);

    if (size > 0x7F) // 127 bytes
    {
        bytes[0] |= 0x80;
        bytes[length++] = static_cast<quint8>(size >> 7 & 0x7F);

        if (size > 0x3FFF) // 16383 bytes
        {
            bytes[1] |= 0x80;
            bytes[length++] = static_cast<quint8>(size >> 14 & 0x7F);

            if (size > 0x1FFFF) // 2097151 bytes
            {
                bytes[2] |= 0x80;
                bytes[length++] = static_cast<quint8>(size >> 21 & 0xFF);
            }
        }
    }

    buffer_.clear();
    buffer_.append(reinterpret_cast<const char*>(bytes), static_cast<qsizetype>(length));

    return buffer_;
}

//--------------------------------------------------------------------------------------------------
// static
QString TcpChannel::errorToString(ErrorCode error_code)
{
    const char* message;

    switch (error_code)
    {
        case ErrorCode::INVALID_PROTOCOL:
            message = QT_TR_NOOP("Violation of the communication protocol.");
            break;

        case ErrorCode::ACCESS_DENIED:
            message = QT_TR_NOOP("Wrong user name or password.");
            break;

        case ErrorCode::CRYPTO_ERROR:
            message = QT_TR_NOOP("Cryptography error (message encryption or decryption failed).");
            break;

        case ErrorCode::SESSION_DENIED:
            message = QT_TR_NOOP("Specified session type is not allowed for the user.");
            break;

        case ErrorCode::VERSION_ERROR:
            message = QT_TR_NOOP("Version of the application you are connecting to is less than "
                                 "the minimum supported version.");
            break;

        case ErrorCode::NETWORK_ERROR:
            message = QT_TR_NOOP("An error occurred with the network (e.g., the network cable was accidentally plugged out).");
            break;

        case ErrorCode::CONNECTION_REFUSED:
            message = QT_TR_NOOP("Connection was refused by the peer (or timed out).");
            break;

        case ErrorCode::REMOTE_HOST_CLOSED:
            message = QT_TR_NOOP("Remote host closed the connection.");
            break;

        case ErrorCode::SPECIFIED_HOST_NOT_FOUND:
            message = QT_TR_NOOP("Host address was not found.");
            break;

        case ErrorCode::SOCKET_TIMEOUT:
            message = QT_TR_NOOP("Socket operation timed out.");
            break;

        case ErrorCode::ADDRESS_IN_USE:
            message = QT_TR_NOOP("Address specified is already in use and was set to be exclusive.");
            break;

        case ErrorCode::ADDRESS_NOT_AVAILABLE:
            message = QT_TR_NOOP("Address specified does not belong to the host.");
            break;

        case ErrorCode::PROXY_AUTH_REQUIRED:
            message = QT_TR_NOOP("Proxy server requires authentication or the credentials are wrong.");
            break;

        case ErrorCode::PROXY_ERROR:
            message = QT_TR_NOOP("Proxy server refused the connection or sent an invalid response.");
            break;

        case ErrorCode::PROXY_DESTINATION_UNREACHABLE:
            message = QT_TR_NOOP("Proxy server could not connect to the destination host.");
            break;

        default:
        {
            if (error_code != ErrorCode::UNKNOWN)
            {
                LOG(ERROR) << "Unknown error code:" << static_cast<int>(error_code);
            }

            message = QT_TR_NOOP("An unknown error occurred.");
        }
        break;
    }

    return tr(message);
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::setPaused(bool enable)
{
    if (!isConnected() || paused_ == enable)
        return;

    paused_ = enable;
    if (paused_)
    {
        keep_alive_state_ = KeepAliveState::INACTIVE;
        return;
    }

    // Keep-alive applies only after authentication completes. Before |authenticated_|
    // is set, |encryptor_| may not be ready, and a KEEP_ALIVE frame would be sent
    // unencrypted, which the peer would treat as a protocol violation.
    if (authenticated_)
        startKeepAliveInterval();

    // We already have an incomplete read operation.
    if (state_ == ReadState::READ_HEADER || state_ == ReadState::READ_DATA ||
        state_ == ReadState::READ_SIZE || state_ == ReadState::READ_SERVICE_HEADER ||
        state_ == ReadState::READ_SERVICE_DATA)
    {
        return;
    }

    // If we have a message that was received before the pause command.
    if (state_ == ReadState::PENDING)
    {
        if (protocol_ == Protocol::LEGACY)
        {
            if (legacy_pending_service_)
                onLegacyServiceMessageReceived();
            else
                onLegacyMessageReceived();
        }
        else
        {
            onMessageReceived();
        }
    }

    doReadHeader();
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::send(quint8 channel_id, const QByteArray& buffer)
{
    if (!io_->alive)
        return;

    // USER_DATA may only be sent over an authenticated channel.
    if (!authenticated_)
    {
        CLOG(ERROR) << "Sending message before authentication completed";
        return;
    }

    addWriteTask(USER_DATA, channel_id, buffer, true);
}

//--------------------------------------------------------------------------------------------------
qint64 TcpChannel::pendingBytes() const
{
    qint64 result = 0;

    for (const QByteArray& buffer : std::as_const(io_->write_queue))
        result += buffer.size();

    return result;
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::tick(TimePoint now)
{
    if (keep_alive_state_ == KeepAliveState::INACTIVE || now < keep_alive_deadline_)
        return;

    if (keep_alive_state_ == KeepAliveState::WAIT_INTERVAL)
    {
        // Incoming traffic since last check or pending outgoing data both prove the
        // connection is alive. Skip the PING and wait out the next interval.
        if (rx_since_last_check_ || !io_->write_queue.isEmpty())
        {
            startKeepAliveInterval();
            return;
        }

        // Channel is idle. Send a PING and require a PONG within kKeepAliveTimeout.
        keep_alive_state_ = KeepAliveState::WAIT_PONG;
        keep_alive_deadline_ = now + kKeepAliveTimeout;

        if (protocol_ == Protocol::LEGACY)
        {
            // The keep-alive of the previous version of the protocol is a service message.
            sendKeepAlive(KEEP_ALIVE_PING, keep_alive_counter_.constData(), keep_alive_counter_.size());
        }
        else
        {
            addWriteTask(KEEP_ALIVE, KEEP_ALIVE_PING, keep_alive_counter_, true);
        }
    }
    else
    {
        // No PONG arrived within the specified period. Forcibly terminate the connection.
        onErrorOccurred(FROM_HERE, ErrorCode::SOCKET_TIMEOUT);
    }
}

//--------------------------------------------------------------------------------------------------
int TcpChannel::speedRx()
{
    TimePoint current_time = Clock::now();
    MilliSeconds duration = DurationCast<MilliSeconds>(current_time - begin_time_rx_);

    speed_rx_ = calculateSpeed(speed_rx_, duration, bytes_rx_);
    begin_time_rx_ = current_time;
    bytes_rx_ = 0;

    return speed_rx_;
}

//--------------------------------------------------------------------------------------------------
int TcpChannel::speedTx()
{
    TimePoint current_time = Clock::now();
    MilliSeconds duration = DurationCast<MilliSeconds>(current_time - begin_time_tx_);

    speed_tx_ = calculateSpeed(speed_tx_, duration, bytes_tx_);
    begin_time_tx_ = current_time;
    bytes_tx_ = 0;

    return speed_tx_;
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::doAuthentication()
{
    if (!authenticator_)
    {
        onErrorOccurred(FROM_HERE, ErrorCode::UNKNOWN);
        return;
    }

    if (!preamble_.isEmpty())
    {
        CLOG(TRACE) << "Sending preamble of" << preamble_.size() << "bytes";

        const bool schedule_write = io_->write_queue.isEmpty();
        io_->write_queue.emplace_back(std::move(preamble_));
        preamble_.clear();
        if (schedule_write)
            doWrite();
    }

    CLOG(TRACE) << "Start authentication";
    authenticator_->start();
    setPaused(false);
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::setPreamble(QByteArray preamble)
{
    preamble_ = std::move(preamble);
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::setConnected(const std::string& peer_address)
{
    connected_ = true;
    address_ = peer_address;
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::onReadCompleted(const std::error_code& error_code, size_t bytes_transferred)
{
    if (error_code)
    {
        if (error_code == asio::error::operation_aborted)
            return;
        onErrorOccurred(FROM_HERE, error_code);
        return;
    }

    addRxBytes(bytes_transferred); // Update RX statistics.

    if (protocol_ == Protocol::LEGACY)
    {
        onLegacyReadCompleted(bytes_transferred);
        return;
    }

    if (state_ == ReadState::READ_HEADER)
    {
        // Until the handshake completes a peer is anonymous, so an inflated header.length
        // would let it pin |kMaxMessageSize| of memory per channel before authenticating.
        // Enforce a much tighter cap for AUTH_DATA - real handshake frames stay well below it.
        const quint32 max_length = authenticated_ ? kMaxMessageSize : kMaxAuthMessageSize;
        if (io_->read_header.length > max_length)
        {
            CLOG(ERROR) << "Too big incoming message:" << io_->read_header.length
                        << "(limit:" << max_length << ")";
            onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
            return;
        }

        if (io_->read_header.length)
            doReadData();
        else
            doReadHeader();
        return;
    }

    CDCHECK(state_ == ReadState::READ_DATA);
    CDCHECK_EQ(bytes_transferred, io_->read_buffer.size());

    if (paused_)
    {
        state_ = ReadState::PENDING;
        return;
    }

    onMessageReceived();

    if (paused_)
    {
        state_ = ReadState::IDLE;
        return;
    }

    doReadHeader();
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::onWriteCompleted(const std::error_code& error_code, size_t bytes_transferred)
{
    if (error_code)
    {
        if (error_code == asio::error::operation_aborted)
            return;
        onErrorOccurred(FROM_HERE, error_code);
        return;
    }

    addTxBytes(bytes_transferred); // Update TX statistics.
    CDCHECK(!io_->write_queue.isEmpty());

    if (write_pool_.size() < kWritePoolReservedSize)
        write_pool_.emplace_back(std::move(io_->write_queue.front()));
    io_->write_queue.pop_front();

    if (!io_->write_queue.isEmpty())
        doWrite();
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::onErrorOccurred(const Location& location, const std::error_code& error_code)
{
    ErrorCode error = ErrorCode::UNKNOWN;

    if (error_code == asio::error::host_not_found)
        error = ErrorCode::SPECIFIED_HOST_NOT_FOUND;
    else if (error_code == asio::error::connection_refused)
        error = ErrorCode::CONNECTION_REFUSED;
    else if (error_code == asio::error::address_in_use)
        error = ErrorCode::ADDRESS_IN_USE;
    else if (error_code == asio::error::timed_out)
        error = ErrorCode::SOCKET_TIMEOUT;
    else if (error_code == asio::error::host_unreachable)
        error = ErrorCode::ADDRESS_NOT_AVAILABLE;
    else if (error_code == asio::error::connection_reset || error_code == asio::error::eof)
        error = ErrorCode::REMOTE_HOST_CLOSED;
    else if (error_code == asio::error::network_down)
        error = ErrorCode::NETWORK_ERROR;
#if defined(Q_OS_WINDOWS)
    // asio puts Win32 system errors (e.g. ERROR_SEM_TIMEOUT - kernel-level TCP keep-alive timeout)
    // into asio::system_category(), which is distinct from std::system_category(). Match against
    // the asio category explicitly.
    else if (error_code.category() == asio::system_category() && error_code.value() == ERROR_SEM_TIMEOUT)
        error = ErrorCode::SOCKET_TIMEOUT;
#endif

    CLOG(INFO) << "Asio error:" << error_code;
    onErrorOccurred(location, error);
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::onErrorOccurred(const Location& location, ErrorCode error_code)
{
    CLOG(INFO) << "Connection finished:" << error_code << "from" << location << "(received" << total_rx_
               << "bytes, sent" << total_tx_ << "bytes)";

    if (!io_->alive)
        return;

    if (authenticator_)
    {
        user_name_ = authenticator_->userName();
        user_id_   = authenticator_->userId();
    }

    setDisconnected();

    io_->alive = false;
    emit sig_errorOccurred(error_code);
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::init()
{
    static_assert(sizeof(Header) == 8, "Header must be 8 bytes without padding");

    CCHECK(authenticator_);
    authenticator_->setParent(this);

    write_pool_.reserve(kWritePoolReservedSize);
    io_->write_queue.reserve(kWriteQueueReservedSize);

    keep_alive_counter_.resize(sizeof(quint32));
    memset(keep_alive_counter_.data(), 0, keep_alive_counter_.size());

    connect(authenticator_, &Authenticator::sig_outgoingMessage,
            this, [this](const QByteArray& data, bool encrypted)
    {
        addWriteTask(AUTH_DATA, 0, data, encrypted);
    });

    connect(authenticator_, &Authenticator::sig_keyChanged, this, [this]()
    {
        // The current version of the protocol always uses AES-256-GCM and rotates the working key
        // from time to time. A session with a peer of the previous version uses the algorithm the
        // peers have agreed on during the handshake and keeps a single key for the whole session.
        const bool legacy = (protocol_ == Protocol::LEGACY);

        CipherType type = CipherType::AES256_GCM;

        if (legacy)
        {
            switch (authenticator_->encryption())
            {
                case proto::key_exchange::ENCRYPTION_AES256_GCM:
                    type = CipherType::AES256_GCM;
                    break;

                case proto::key_exchange::ENCRYPTION_CHACHA20_POLY1305:
                    type = CipherType::CHACHA20_POLY1305;
                    break;

                default:
                    CLOG(ERROR) << "Unknown encryption:" << authenticator_->encryption();
                    onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
                    return;
            }
        }

        encryptor_ = StreamEncryptor::create(
            type,
            authenticator_->sessionKey(Authenticator::Direction::ENCRYPT),
            authenticator_->iv(Authenticator::Direction::ENCRYPT),
            !legacy);
        decryptor_ = StreamDecryptor::create(
            type,
            authenticator_->sessionKey(Authenticator::Direction::DECRYPT),
            authenticator_->iv(Authenticator::Direction::DECRYPT),
            !legacy);

        if (!encryptor_ || !decryptor_)
        {
            onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
            return;
        }
    });

    connect(authenticator_, &Authenticator::sig_finished, this, [this](Authenticator::ErrorCode error_code)
    {
        setPaused(true);

        switch (error_code)
        {
            case Authenticator::ErrorCode::SUCCESS:
                break;
            case Authenticator::ErrorCode::PROTOCOL_ERROR:
                onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
                return;
            case Authenticator::ErrorCode::SESSION_DENIED:
                onErrorOccurred(FROM_HERE, ErrorCode::SESSION_DENIED);
                return;
            case Authenticator::ErrorCode::VERSION_ERROR:
                onErrorOccurred(FROM_HERE, ErrorCode::VERSION_ERROR);
                return;
            case Authenticator::ErrorCode::ACCESS_DENIED:
                onErrorOccurred(FROM_HERE, ErrorCode::ACCESS_DENIED);
                return;
            default:
                onErrorOccurred(FROM_HERE, ErrorCode::UNKNOWN);
                return;
        }

        version_       = authenticator_->peerVersion();
        os_name_       = authenticator_->peerOsName();
        computer_name_ = authenticator_->peerComputerName();
        display_name_  = authenticator_->peerDisplayName();
        architecture_  = authenticator_->peerArch();
        user_name_     = authenticator_->userName();
        user_id_       = authenticator_->userId();
        session_type_  = authenticator_->sessionType();

        // The peers of the previous version of the protocol do not send the channel id until the
        // handshake is over.
        if (protocol_ == Protocol::LEGACY)
        {
            CLOG(INFO) << "Support for channel id is enabled";
            channel_id_supported_ = true;
        }

        authenticator_->disconnect();
        authenticated_ = true;

        emit sig_authenticated();

        // Delete (deleteLater) the object after emitting a signal indicating successful
        // authentication. Do not change this order!
        authenticator_.reset();
    });
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::setDisconnected()
{
    connected_ = false;
    keep_alive_state_ = KeepAliveState::INACTIVE;
    closeTransport();
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::onMessageReceived()
{
    if (io_->read_buffer.isEmpty())
    {
        onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
        return;
    }

    if (io_->read_buffer.size() > kMaxMessageSize ||
        io_->read_buffer.size() != qsizetype(io_->read_header.length))
    {
        CLOG(INFO) << "Invalid message length:" << io_->read_header.length;
        onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
        return;
    }

    if (io_->read_header.type == AUTH_DATA)
    {
        if (!authenticator_ || authenticated_)
        {
            onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
            return;
        }

        if (!decryptor_)
        {
            authenticator_->onIncomingMessage(io_->read_buffer);
            return;
        }

        resizeBuffer(&decrypt_buffer_, decryptor_->decryptedDataSize(io_->read_buffer.size()));

        if (!decryptor_->decrypt(io_->read_buffer.data(), io_->read_buffer.size(), // Data
                                 &io_->read_header, sizeof(io_->read_header), // AAD
                                 decrypt_buffer_.data()))
        {
            onErrorOccurred(FROM_HERE, ErrorCode::ACCESS_DENIED);
            return;
        }

        authenticator_->onIncomingMessage(decrypt_buffer_);
        return;
    }

    // Any non-AUTH_DATA message before authentication completes is a protocol violation.
    if (!authenticated_)
    {
        onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
        return;
    }

    // Defense in depth: |decryptor_| must be created before |authenticated_| is set
    // (in Authenticator::sig_keyChanged handler), but guard against any state inconsistency.
    if (!decryptor_)
    {
        onErrorOccurred(FROM_HERE, ErrorCode::CRYPTO_ERROR);
        return;
    }

    resizeBuffer(&decrypt_buffer_, decryptor_->decryptedDataSize(io_->read_buffer.size()));

    if (!decryptor_->decrypt(io_->read_buffer.data(), io_->read_buffer.size(), // Data
                             &io_->read_header, sizeof(io_->read_header), // AAD
                             decrypt_buffer_.data()))
    {
        onErrorOccurred(FROM_HERE, ErrorCode::CRYPTO_ERROR);
        return;
    }

    // Any successfully decrypted message proves the peer is alive. Cheap flag instead of
    // rescheduling the timer on every message - the keep-alive callbacks will consult it
    // when they actually fire.
    rx_since_last_check_ = true;

    if (io_->read_header.type == USER_DATA)
    {
        emit sig_messageReceived(io_->read_header.param1, decrypt_buffer_);
    }
    else if (io_->read_header.type == KEEP_ALIVE)
    {
        if (io_->read_header.param1 & KEEP_ALIVE_PING)
        {
            addWriteTask(KEEP_ALIVE, KEEP_ALIVE_PONG, decrypt_buffer_, true);
            return;
        }

        if (decrypt_buffer_.size() != keep_alive_counter_.size())
        {
            onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
            return;
        }

        // Pong must contain the same data as ping.
        if (decrypt_buffer_ != keep_alive_counter_)
        {
            onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
            return;
        }

        // Increase the counter of sent packets.
        largeNumberIncrement(&keep_alive_counter_);

        // PONG received - switch back to the interval cycle.
        startKeepAliveInterval();
    }
    else
    {
        // Ignore unknown types.
    }
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::onLegacyReadCompleted(size_t bytes_transferred)
{
    if (state_ == ReadState::READ_SIZE)
    {
        // The size of a frame is written as a variable length integer, one byte at a time.
        if (!io_->size_reader.addByte(size_byte_))
        {
            doReadLegacySize();
            return;
        }

        const size_t message_size = io_->size_reader.messageSize();
        io_->size_reader.reset();

        const size_t max_size = authenticated_ ? kMaxMessageSize : kMaxAuthMessageSize;
        if (message_size > max_size)
        {
            CLOG(ERROR) << "Too big incoming message:" << message_size << "(limit:" << max_size << ")";
            onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
            return;
        }

        // A size of zero means that a service message follows.
        if (!message_size)
        {
            doReadLegacyServiceHeader();
            return;
        }

        resizeBuffer(&io_->read_buffer, static_cast<qint64>(message_size));

        state_ = ReadState::READ_DATA;
        readTransport(io_->read_buffer.data(), io_->read_buffer.size());
        return;
    }

    if (state_ == ReadState::READ_SERVICE_HEADER)
    {
        CDCHECK_EQ(io_->read_buffer.size(), sizeof(ServiceHeader));
        CDCHECK_EQ(bytes_transferred, io_->read_buffer.size());

        const ServiceHeader* header = reinterpret_cast<const ServiceHeader*>(io_->read_buffer.data());

        const size_t max_size = authenticated_ ? kMaxMessageSize : kMaxAuthMessageSize;
        if (header->length > max_size)
        {
            CLOG(ERROR) << "Too big service message:" << header->length << "(limit:" << max_size << ")";
            onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
            return;
        }

        if (header->type == SERVICE_KEEP_ALIVE && !header->length)
        {
            // A keep-alive message must always contain the data.
            onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
            return;
        }

        doReadLegacyServiceData(header->length);
        return;
    }

    if (state_ == ReadState::READ_DATA)
    {
        CDCHECK_EQ(bytes_transferred, io_->read_buffer.size());

        if (paused_)
        {
            state_ = ReadState::PENDING;
            legacy_pending_service_ = false;
            return;
        }

        onLegacyMessageReceived();

        if (paused_)
        {
            state_ = ReadState::IDLE;
            return;
        }

        doReadLegacySize();
        return;
    }

    CDCHECK(state_ == ReadState::READ_SERVICE_DATA);

    if (paused_)
    {
        state_ = ReadState::PENDING;
        legacy_pending_service_ = true;
        return;
    }

    onLegacyServiceMessageReceived();

    if (paused_)
    {
        state_ = ReadState::IDLE;
        return;
    }

    doReadLegacySize();
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::onLegacyMessageReceived()
{
    char* read_data = io_->read_buffer.data();
    size_t read_size = io_->read_buffer.size();

    quint8 channel_id = 0;

    // The peers of the previous version of the protocol do not send the channel id until the
    // handshake is over.
    if (channel_id_supported_)
    {
        if (read_size < sizeof(UserDataHeader))
        {
            onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
            return;
        }

        UserDataHeader header;
        memcpy(&header, read_data, sizeof(header));

        read_data += sizeof(header);
        read_size -= sizeof(header);

        channel_id = header.channel_id;
    }

    if (!read_size)
    {
        onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
        return;
    }

    QByteArray message;

    if (decryptor_)
    {
        resizeBuffer(&decrypt_buffer_, decryptor_->decryptedDataSize(read_size));

        if (!decryptor_->decrypt(read_data, read_size, decrypt_buffer_.data()))
        {
            onErrorOccurred(FROM_HERE, ErrorCode::CRYPTO_ERROR);
            return;
        }

        message = decrypt_buffer_;
    }
    else
    {
        message = QByteArray::fromRawData(read_data, read_size);
    }

    // Until the handshake is over all the messages belong to the authenticator.
    if (!authenticated_)
    {
        if (!authenticator_)
        {
            onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
            return;
        }

        authenticator_->onIncomingMessage(message);
        return;
    }

    // Any successfully decrypted message proves the peer is alive.
    rx_since_last_check_ = true;

    emit sig_messageReceived(channel_id, message);
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::onLegacyServiceMessageReceived()
{
    const ServiceHeader* header = reinterpret_cast<const ServiceHeader*>(io_->read_buffer.data());

    if (header->type != SERVICE_KEEP_ALIVE)
    {
        // Ignore the unknown service messages.
        return;
    }

    const char* data = io_->read_buffer.data() + sizeof(ServiceHeader);

    if (header->flags & KEEP_ALIVE_PING)
    {
        // The pong must contain the same data as the ping.
        sendKeepAlive(KEEP_ALIVE_PONG, data, header->length);
        return;
    }

    if (header->length != keep_alive_counter_.size() ||
        memcmp(data, keep_alive_counter_.constData(), keep_alive_counter_.size()) != 0)
    {
        onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
        return;
    }

    // Increase the counter of sent packets.
    largeNumberIncrement(&keep_alive_counter_);

    // PONG received - switch back to the interval cycle.
    startKeepAliveInterval();
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::sendKeepAlive(quint8 flags, const void* data, size_t size)
{
    ServiceHeader header;
    memset(&header, 0, sizeof(header));

    header.type   = SERVICE_KEEP_ALIVE;
    header.flags  = flags;
    header.length = static_cast<quint32>(size);

    QByteArray buffer;
    buffer.resize(static_cast<qsizetype>(sizeof(quint8) + sizeof(header) + size));

    // The first byte set to 0 indicates that this is a service message.
    buffer[0] = 0;

    memcpy(buffer.data() + sizeof(quint8), &header, sizeof(header));
    memcpy(buffer.data() + sizeof(quint8) + sizeof(header), data, size);

    const bool schedule_write = io_->write_queue.isEmpty();
    io_->write_queue.emplace_back(std::move(buffer));
    if (schedule_write)
        doWrite();
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::doReadLegacySize()
{
    state_ = ReadState::READ_SIZE;
    readTransport(&size_byte_, sizeof(size_byte_));
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::doReadLegacyServiceHeader()
{
    resizeBuffer(&io_->read_buffer, sizeof(ServiceHeader));

    state_ = ReadState::READ_SERVICE_HEADER;
    readTransport(io_->read_buffer.data(), io_->read_buffer.size());
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::doReadLegacyServiceData(size_t length)
{
    io_->read_buffer.resize(io_->read_buffer.size() + static_cast<qsizetype>(length));

    state_ = ReadState::READ_SERVICE_DATA;
    readTransport(io_->read_buffer.data() + sizeof(ServiceHeader),
                  io_->read_buffer.size() - sizeof(ServiceHeader));
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::addWriteTask(quint8 type, quint8 param, const QByteArray& data, bool encrypted)
{
    if (data.isEmpty())
    {
        onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
        return;
    }

    // Only AUTH_DATA may bypass the authenticated state (it carries the handshake itself).
    if (type != AUTH_DATA && !authenticated_)
    {
        onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
        return;
    }

    // Encryption decision is per-task: the authenticator marks individual handshake frames
    // as plaintext or encrypted, and USER_DATA / KEEP_ALIVE are always encrypted.
    if (encrypted && !encryptor_)
    {
        onErrorOccurred(FROM_HERE, ErrorCode::CRYPTO_ERROR);
        return;
    }

    if (protocol_ == Protocol::LEGACY)
    {
        addLegacyWriteTask(type, param, data, encrypted);
        return;
    }

    const qint64 target_data_size = encrypted ?
        encryptor_->encryptedDataSize(data.size()) : data.size();
    const qint64 total_size = target_data_size + static_cast<qint64>(sizeof(Header));
    const qint64 max_size = (type == AUTH_DATA && !authenticated_) ?
        kMaxAuthMessageSize : kMaxMessageSize;

    if (total_size > max_size)
    {
        CLOG(ERROR) << "Too big outgoing message:" << total_size << "(limit:" << max_size << ")";
        onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
        return;
    }

    Header header;
    header.type = type;
    header.param1 = param;
    header.param2 = 0;
    header.param3 = 0;
    header.length = static_cast<quint32>(target_data_size);

    QByteArray write_buffer;

    if (!write_pool_.isEmpty())
    {
        write_buffer = std::move(write_pool_.front());
        write_pool_.pop_front();

        if (write_buffer.capacity() < total_size)
            write_buffer.reserve(total_size);
    }

    write_buffer.resize(total_size);

    memcpy(write_buffer.data(), &header, sizeof(Header));

    if (encrypted)
    {
        if (!encryptor_->encrypt(data.data(), data.size(), // Data
                                 &header, sizeof(Header), // AAD
                                 write_buffer.data() + sizeof(Header)))
        {
            onErrorOccurred(FROM_HERE, ErrorCode::CRYPTO_ERROR);
            return;
        }
    }
    else
    {
        memcpy(write_buffer.data() + sizeof(Header), data.data(), data.size());
    }

    const bool schedule_write = io_->write_queue.isEmpty();
    io_->write_queue.emplace_back(std::move(write_buffer));
    if (schedule_write)
        doWrite();
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::addLegacyWriteTask(quint8 type, quint8 param, const QByteArray& data, bool encrypted)
{
    if (type == KEEP_ALIVE)
    {
        // The keep-alive of the previous version of the protocol is a service message, it is
        // never encrypted.
        sendKeepAlive(param, data.constData(), data.size());
        return;
    }

    const qint64 target_data_size = encrypted ?
        encryptor_->encryptedDataSize(data.size()) : data.size();

    // The channel id is a part of the frame only after the handshake is over: the peers of the
    // previous version of the protocol do not understand it before that.
    const bool with_channel_id = (type == USER_DATA) && channel_id_supported_;

    const qint64 total_size = target_data_size +
        (with_channel_id ? static_cast<qint64>(sizeof(UserDataHeader)) : 0);

    const qint64 max_size = (type == AUTH_DATA && !authenticated_) ?
        kMaxAuthMessageSize : kMaxMessageSize;

    if (total_size > max_size)
    {
        CLOG(ERROR) << "Too big outgoing message:" << total_size << "(limit:" << max_size << ")";
        onErrorOccurred(FROM_HERE, ErrorCode::INVALID_PROTOCOL);
        return;
    }

    const QByteArray& size = size_writer_.variableSize(static_cast<size_t>(total_size));

    QByteArray write_buffer;

    if (!write_pool_.isEmpty())
    {
        write_buffer = std::move(write_pool_.front());
        write_pool_.pop_front();

        if (write_buffer.capacity() < size.size() + total_size)
            write_buffer.reserve(size.size() + total_size);
    }

    write_buffer.resize(size.size() + total_size);

    // Copy the size of the message to the buffer.
    memcpy(write_buffer.data(), size.constData(), size.size());

    char* payload = write_buffer.data() + size.size();

    if (with_channel_id)
    {
        UserDataHeader header;
        header.channel_id = param;
        header.reserved = 0;

        // Copy the channel id to the buffer.
        memcpy(payload, &header, sizeof(header));
        payload += sizeof(header);
    }

    if (encrypted)
    {
        if (!encryptor_->encrypt(data.data(), data.size(), payload))
        {
            onErrorOccurred(FROM_HERE, ErrorCode::CRYPTO_ERROR);
            return;
        }
    }
    else
    {
        memcpy(payload, data.data(), data.size());
    }

    const bool schedule_write = io_->write_queue.isEmpty();
    io_->write_queue.emplace_back(std::move(write_buffer));
    if (schedule_write)
        doWrite();
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::doWrite()
{
    const QByteArray& buffer = io_->write_queue.front();
    writeTransport(buffer.data(), buffer.size());
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::doReadHeader()
{
    if (protocol_ == Protocol::LEGACY)
    {
        doReadLegacySize();
        return;
    }

    state_ = ReadState::READ_HEADER;
    readTransport(&io_->read_header, sizeof(Header));
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::doReadData()
{
    resizeBuffer(&io_->read_buffer, io_->read_header.length);
    state_ = ReadState::READ_DATA;
    readTransport(io_->read_buffer.data(), io_->read_buffer.size());
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::startKeepAliveInterval()
{
    rx_since_last_check_ = false;
    keep_alive_state_ = KeepAliveState::WAIT_INTERVAL;
    keep_alive_deadline_ = Clock::now() + kKeepAliveInterval;
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::addTxBytes(size_t bytes_count)
{
    bytes_tx_ += bytes_count;
    total_tx_ += bytes_count;
}

//--------------------------------------------------------------------------------------------------
void TcpChannel::addRxBytes(size_t bytes_count)
{
    bytes_rx_ += bytes_count;
    total_rx_ += bytes_count;
}
