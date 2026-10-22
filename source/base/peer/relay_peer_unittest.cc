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

#include <gtest/gtest.h>

#include "base/crypto/generic_hash.h"
#include "base/crypto/key_pair.h"
#include "base/crypto/secure_byte_array.h"
#include "base/crypto/stream_decryptor.h"
#include "base/peer/client_authenticator.h"
#include "base/peer/relay_peer.h"
#include "base/serialization.h"
#include "proto/relay_peer.h"
#include "proto/router_client.h"

// The message a peer sends to the relay is built by the peer itself, and the relay only matches
// the messages of the two peers of a session. The peers of the previous version of the program
// understand only ChaCha20-Poly1305 and derive the key of the session with the relay another way,
// so the message has to be the one they expect - otherwise the relay never pairs the peers and
// the session never starts.
class RelayPeerTestPeer
{
public:
    static QByteArray authenticationMessage(const proto::router::RelayKey& key, const std::string& secret)
    {
        RelayPeer peer(new ClientAuthenticator());
        return peer.authenticationMessage(key, secret);
    }
};

namespace {

constexpr quint32 kKeyId = 42;

// The key of the relay: the peer encrypts the secret of the session with the key that both of them
// derive from the public key of the relay.
proto::router::RelayKey makeRelayKey(const KeyPair& relay_key_pair, const QByteArray& iv,
                                     proto::router::RelayKey::Encryption encryption)
{
    proto::router::RelayKey key;
    key.set_type(proto::router::RelayKey::TYPE_X25519);
    key.set_key_id(kKeyId);
    key.set_public_key(relay_key_pair.publicKey().toStdString());
    key.set_iv(iv.toStdString());
    key.set_encryption(encryption);
    return key;
}

// Reads the message of the peer and gives the secret back, decrypted the way the relay does.
std::optional<QByteArray> readSecret(const QByteArray& message, const KeyPair& relay_key_pair,
                                     const QByteArray& iv, CipherType cipher, bool bound_to_public_keys)
{
    proto::relay::PeerToRelay peer_to_relay;
    if (!parse(message, &peer_to_relay))
        return std::nullopt;

    const SecureByteArray shared_secret(
        relay_key_pair.sessionKey(QByteArray::fromStdString(peer_to_relay.public_key())));
    if (shared_secret.isEmpty())
        return std::nullopt;

    SecureByteArray session_key;

    if (bound_to_public_keys)
    {
        GenericHash hash(GenericHash::Type::SHA256);
        hash.addData(shared_secret);
        hash.addData(relay_key_pair.publicKey());
        hash.addData(QByteArray::fromStdString(peer_to_relay.public_key()));
        session_key = SecureByteArray(hash.result());
    }
    else
    {
        session_key = SecureByteArray(GenericHash::hash(GenericHash::Type::BLAKE2s256,
                                                        shared_secret.toByteArray()));
    }

    auto decryptor = StreamDecryptor::create(cipher, session_key, iv, false);
    if (!decryptor)
        return std::nullopt;

    const std::string encrypted = peer_to_relay.data();

    QByteArray decrypted(decryptor->decryptedDataSize(static_cast<qint64>(encrypted.size())), '\0');
    if (!decryptor->decrypt(encrypted.data(), static_cast<qint64>(encrypted.size()),
                            decrypted.data()))
    {
        return std::nullopt;
    }

    return decrypted;
}

} // namespace

//--------------------------------------------------------------------------------------------------
// The peers of the current version of the program talk to the relay with AES-256-GCM, and the key of
// the session with the relay is bound to both public keys.
TEST(RelayPeerTest, TheModernPeerUsesAesAndTheBoundKey)
{
    KeyPair relay_key_pair = KeyPair::create(KeyPair::Type::X25519);
    ASSERT_TRUE(relay_key_pair.isValid());

    const QByteArray iv = QByteArray(12, '\x42');
    const std::string secret = "the-secret-of-the-session";

    const proto::router::RelayKey key =
        makeRelayKey(relay_key_pair, iv, proto::router::RelayKey::ENCRYPTION_AES256_GCM);

    const QByteArray message = RelayPeerTestPeer::authenticationMessage(key, secret);
    ASSERT_FALSE(message.isEmpty());

    proto::relay::PeerToRelay peer_to_relay;
    ASSERT_TRUE(parse(message, &peer_to_relay));
    EXPECT_EQ(peer_to_relay.key_id(), kKeyId);
    EXPECT_EQ(peer_to_relay.encryption(), proto::relay::PeerToRelay::ENCRYPTION_AES256_GCM);

    const std::optional<QByteArray> decrypted = readSecret(message, relay_key_pair, iv,
                                                           CipherType::AES256_GCM, true);
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(decrypted.value(), QByteArray(secret.data(), static_cast<int>(secret.size())));
}

//--------------------------------------------------------------------------------------------------
// The peers of the previous version of the program talk to the relay with ChaCha20-Poly1305, and
// the key of the session with the relay is the bare shared secret. The relay matches the messages
// of the two peers byte for byte, so both have to come out the same.
TEST(RelayPeerTest, TheLegacyPeerUsesChaCha20AndTheBareKey)
{
    KeyPair relay_key_pair = KeyPair::create(KeyPair::Type::X25519);
    ASSERT_TRUE(relay_key_pair.isValid());

    const QByteArray iv = QByteArray(12, '\x42');
    const std::string secret = "the-secret-of-the-session";

    const proto::router::RelayKey key =
        makeRelayKey(relay_key_pair, iv, proto::router::RelayKey::ENCRYPTION_CHACHA20_POLY1305);

    const QByteArray message = RelayPeerTestPeer::authenticationMessage(key, secret);
    ASSERT_FALSE(message.isEmpty());

    proto::relay::PeerToRelay peer_to_relay;
    ASSERT_TRUE(parse(message, &peer_to_relay));
    EXPECT_EQ(peer_to_relay.key_id(), kKeyId);
    EXPECT_EQ(peer_to_relay.encryption(), proto::relay::PeerToRelay::ENCRYPTION_CHACHA20_POLY1305);

    const std::optional<QByteArray> decrypted = readSecret(message, relay_key_pair, iv,
                                                           CipherType::CHACHA20_POLY1305, false);
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(decrypted.value(), QByteArray(secret.data(), static_cast<int>(secret.size())));
}

//--------------------------------------------------------------------------------------------------
// The two peers of a session have to come out with the same message, or the relay never pairs them.
TEST(RelayPeerTest, TheLegacyPeerAndTheModernPeerDoNotMatch)
{
    KeyPair relay_key_pair = KeyPair::create(KeyPair::Type::X25519);
    ASSERT_TRUE(relay_key_pair.isValid());

    const QByteArray iv = QByteArray(12, '\x42');
    const std::string secret = "the-secret-of-the-session";

    const QByteArray legacy_message = RelayPeerTestPeer::authenticationMessage(
        makeRelayKey(relay_key_pair, iv, proto::router::RelayKey::ENCRYPTION_CHACHA20_POLY1305), secret);
    const QByteArray modern_message = RelayPeerTestPeer::authenticationMessage(
        makeRelayKey(relay_key_pair, iv, proto::router::RelayKey::ENCRYPTION_AES256_GCM), secret);

    ASSERT_FALSE(legacy_message.isEmpty());
    ASSERT_FALSE(modern_message.isEmpty());
    EXPECT_NE(legacy_message, modern_message);
}
