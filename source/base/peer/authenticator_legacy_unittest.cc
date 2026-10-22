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

#include <deque>

#include "base/shared_pointer.h"
#include "base/crypto/secure_string.h"
#include "base/crypto/stream_decryptor.h"
#include "base/crypto/stream_encryptor.h"
#include "base/peer/client_authenticator_legacy.h"
#include "base/peer/server_authenticator_legacy.h"
#include "base/peer/user.h"
#include "base/peer/user_list.h"
#include "proto/key_exchange.h"

namespace {

const char kUserName[] = "operator";
const char kPassword[] = "Password1234!";

constexpr quint32 kSessionType = 1;

// A user list with a single user - the same thing the router hands to its server, minus the
// database behind it.
class TestUserList final : public UserList
{
public:
    User find(const QString& username) const final
    {
        if (username.compare(user_.name, Qt::CaseInsensitive) != 0)
            return User::kInvalidUser;
        return user_;
    }

    QByteArray seedKey() const final { return seed_key_; }
    void setSeedKey(const QByteArray& seed_key) final { seed_key_ = seed_key; }

    void setUser(const User& user) { user_ = user; }

private:
    User user_;
    QByteArray seed_key_ = QByteArrayLiteral("seed-key-for-tests");
};

// The algorithm the peers have agreed on during the handshake.
CipherType cipherOf(const Authenticator& authenticator)
{
    return (authenticator.encryption() == proto::key_exchange::ENCRYPTION_AES256_GCM) ?
        CipherType::AES256_GCM : CipherType::CHACHA20_POLY1305;
}

// The messages of a session travel through the channel, so a message never arrives while the
// other side is still sending. The test keeps the same order: the messages are put into a queue
// and delivered when the sending is over.
class MessageQueue
{
public:
    void push(Authenticator* from, Authenticator* to, const QByteArray& data, bool encrypted)
    {
        queue_.push_back(Item { from, to, data, encrypted });
    }

    void flush()
    {
        while (!queue_.empty())
        {
            const Item item = queue_.front();
            queue_.pop_front();

            deliver(item.from, item.to, item.data, item.encrypted);
        }
    }

private:
    struct Item
    {
        Authenticator* from;
        Authenticator* to;
        QByteArray data;
        bool encrypted;
    };

    // The channel encrypts the messages of a session, the test does it here.
    static void deliver(Authenticator* from, Authenticator* to, const QByteArray& data, bool encrypted)
    {
        if (!encrypted)
        {
            to->onIncomingMessage(data);
            return;
        }

        const CipherType type = cipherOf(*from);

        auto encryptor = StreamEncryptor::create(type,
            from->sessionKey(Authenticator::Direction::ENCRYPT),
            from->iv(Authenticator::Direction::ENCRYPT),
            false);
        EXPECT_TRUE(encryptor);

        QByteArray encrypted_data(encryptor->encryptedDataSize(data.size()), '\0');
        EXPECT_TRUE(encryptor->encrypt(data.constData(), data.size(), encrypted_data.data()));

        auto decryptor = StreamDecryptor::create(type,
            to->sessionKey(Authenticator::Direction::DECRYPT),
            to->iv(Authenticator::Direction::DECRYPT),
            false);
        EXPECT_TRUE(decryptor);

        QByteArray decrypted_data(decryptor->decryptedDataSize(encrypted_data.size()), '\0');
        EXPECT_TRUE(decryptor->decrypt(encrypted_data.constData(), encrypted_data.size(),
                                       decrypted_data.data()));

        to->onIncomingMessage(decrypted_data);
    }

    std::deque<Item> queue_;
};

// Brings a client and a server of the previous version of the protocol to a common session.
void runHandshake(ClientAuthenticatorLegacy& client, ServerAuthenticatorLegacy& server)
{
    MessageQueue queue;

    QObject::connect(&client, &Authenticator::sig_outgoingMessage, &client,
                     [&](const QByteArray& data, bool encrypted)
    {
        queue.push(&client, &server, data, encrypted);
    });

    QObject::connect(&server, &Authenticator::sig_outgoingMessage, &server,
                     [&](const QByteArray& data, bool encrypted)
    {
        queue.push(&server, &client, data, encrypted);
    });

    server.start();
    client.start();

    queue.flush();
}

} // namespace

//--------------------------------------------------------------------------------------------------
// The handshake of the previous version of the protocol: the client and the server must agree on
// the session key, otherwise the peers of that version can not read the traffic of the session.
TEST(AuthenticatorLegacyTest, ClientAndServerAgreeOnTheSessionKey)
{
    User user = User::create(QLatin1String(kUserName), SecureString(QLatin1String(kPassword)));
    user.sessions = kSessionType;
    user.flags = User::ENABLED;

    SharedPointer<TestUserList> user_list(new TestUserList());
    user_list->setUser(user);

    ClientAuthenticatorLegacy client;
    client.setIdentify(proto::key_exchange::IDENTIFY_SRP);
    client.setUserName(QLatin1String(kUserName));
    client.setPassword(SecureString(QLatin1String(kPassword)));
    client.setSessionType(kSessionType);

    ServerAuthenticatorLegacy server;
    server.setUserList(user_list);

    runHandshake(client, server);

    EXPECT_EQ(client.state(), Authenticator::State::SUCCESS);
    EXPECT_EQ(server.state(), Authenticator::State::SUCCESS);

    EXPECT_EQ(client.userName(), std::string());
    EXPECT_EQ(server.userName(), std::string(kUserName));

    // The server learned who the client is.
    EXPECT_FALSE(server.peerComputerName().empty());
    EXPECT_FALSE(server.peerOsName().empty());

    // The key of the session must be the same on both ends. The peers of the previous version of
    // the protocol use the master as it is, without a label of the direction.
    const SecureByteArray client_key = client.sessionKey(Authenticator::Direction::ENCRYPT);
    const SecureByteArray server_key = server.sessionKey(Authenticator::Direction::DECRYPT);

    EXPECT_FALSE(client_key.isEmpty());
    EXPECT_EQ(client_key.toByteArray(), server_key.toByteArray());

    // The peers of the previous version of the protocol do not rotate the key during the session.
    EXPECT_EQ(client.sessionKey(Authenticator::Direction::ENCRYPT).toByteArray(),
              client.sessionKey(Authenticator::Direction::DECRYPT).toByteArray());
}

//--------------------------------------------------------------------------------------------------
// The peers of the previous version of the protocol must be able to read each other, so the cipher
// of the session must be one of the two algorithms of that version.
TEST(AuthenticatorLegacyTest, TheCipherOfTheSessionIsOneOfTheTwoOfThePreviousVersion)
{
    User user = User::create(QLatin1String(kUserName), SecureString(QLatin1String(kPassword)));
    user.sessions = kSessionType;
    user.flags = User::ENABLED;

    SharedPointer<TestUserList> user_list(new TestUserList());
    user_list->setUser(user);

    ClientAuthenticatorLegacy client;
    client.setIdentify(proto::key_exchange::IDENTIFY_SRP);
    client.setUserName(QLatin1String(kUserName));
    client.setPassword(SecureString(QLatin1String(kPassword)));
    client.setSessionType(kSessionType);

    ServerAuthenticatorLegacy server;
    server.setUserList(user_list);

    runHandshake(client, server);

    EXPECT_TRUE(client.encryption() == proto::key_exchange::ENCRYPTION_AES256_GCM ||
                client.encryption() == proto::key_exchange::ENCRYPTION_CHACHA20_POLY1305);
    EXPECT_EQ(client.encryption(), server.encryption());
}
