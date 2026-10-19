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

#include <QFile>
#include <QTemporaryDir>

#include "base/logging.h"
#include "router/settings.h"
#include "proto/router.h"

namespace {

// The settings of the router, against a configuration file of the test's own.
//
// The path of the file comes from the environment rather than from an argument, so every read
// goes through the same code a running router does - a file that is opened, parsed and closed
// once per Settings, not a value handed over.
class SettingsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(dir_.isValid());

        path_ = dir_.filePath(QStringLiteral("router.conf"));
        old_override_ = qEnvironmentVariable("ASPIA_ROUTER_CONFIG_FILE");
        qputenv("ASPIA_ROUTER_CONFIG_FILE", path_.toStdString().c_str());
    }

    void TearDown() override
    {
        if (old_override_.isEmpty())
        {
            qunsetenv("ASPIA_ROUTER_CONFIG_FILE");
        }
        else
        {
            qputenv("ASPIA_ROUTER_CONFIG_FILE", old_override_.toStdString().c_str());
        }
    }

    void writeConfig(const QByteArray& content) const
    {
        QFile file(path_);

        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_TRUE(file.write(content) == content.size());
    }

    QTemporaryDir dir_;
    QString path_;
    QString old_override_;
};

} // namespace

//--------------------------------------------------------------------------------------------------
// A configuration that never mentioned the second factor keeps every role protected. This is the
// one default worth being strict about: an account that opened because a key was absent would be
// an account nobody decided to open.
TEST_F(SettingsTest, RolesAreProtectedWhenTheFileSaysNothing)
{
    writeConfig("[router]\n");

    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(settings.isTwoFactorForManagerEnabled());
    EXPECT_TRUE(settings.isTwoFactorForOperatorEnabled());
}

//--------------------------------------------------------------------------------------------------
// A file that does not exist at all is the state of a fresh installation, and it must not be read
// as a permission to let anybody in without a code.
TEST_F(SettingsTest, RolesAreProtectedWhenThereIsNoFileAtAll)
{
    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(settings.isTwoFactorForManagerEnabled());
    EXPECT_TRUE(settings.isTwoFactorForOperatorEnabled());
}

//--------------------------------------------------------------------------------------------------
// The three are independent of each other. One being switched off is the normal case and must not
// carry the other two with it - that is the whole reason for three settings and not one.
TEST_F(SettingsTest, RolesAreSwitchedOffOneByOne)
{
    writeConfig("[router]\ntwo_factor_manager=false\n");

    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorForAdminEnabled());
    EXPECT_FALSE(settings.isTwoFactorForManagerEnabled());
    EXPECT_TRUE(settings.isTwoFactorForOperatorEnabled());

    writeConfig("[router]\ntwo_factor_admin=false\ntwo_factor_operator=false\n");

    const Settings second;

    EXPECT_FALSE(second.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(second.isTwoFactorForManagerEnabled());
    EXPECT_FALSE(second.isTwoFactorForOperatorEnabled());
}

//--------------------------------------------------------------------------------------------------
// A hand written file spells the value out, while the file the router writes itself uses 1 and 0.
// Both have to mean the same thing, since the first is what an administrator writes and the
// second is what the router puts there.
TEST_F(SettingsTest, ValuesAreReadInBothSpellings)
{
    writeConfig("[router]\ntwo_factor_admin=true\ntwo_factor_manager=0\n"
                "two_factor_operator=false\n");

    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorForAdminEnabled());
    EXPECT_FALSE(settings.isTwoFactorForManagerEnabled());
    EXPECT_FALSE(settings.isTwoFactorForOperatorEnabled());
}

//--------------------------------------------------------------------------------------------------
// A value that is neither of the two is not a request to let anybody in: a mistyped key, a value
// left half-edited, a file written by something that did not know this setting. The protected
// answer is the only one that cannot be reached by accident.
TEST_F(SettingsTest, UnreadableValueKeepsTheRoleProtected)
{
    writeConfig("[router]\ntwo_factor_admin=yes\ntwo_factor_manager=\ntwo_factor_operator=2\n");

    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(settings.isTwoFactorForManagerEnabled());
    EXPECT_TRUE(settings.isTwoFactorForOperatorEnabled());
}

//--------------------------------------------------------------------------------------------------
// The role a session opens as is the value the switch is looked up by, so the three settings and
// the three roles have to meet at the right place.
TEST_F(SettingsTest, TheRoleOfTheSessionDecidesTheValue)
{
    writeConfig("[router]\ntwo_factor_operator=false\n");

    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorEnabledFor(proto::router::SESSION_TYPE_ADMIN));
    EXPECT_TRUE(settings.isTwoFactorEnabledFor(proto::router::SESSION_TYPE_MANAGER));
    EXPECT_FALSE(settings.isTwoFactorEnabledFor(proto::router::SESSION_TYPE_OPERATOR));
}

//--------------------------------------------------------------------------------------------------
// A role this build does not name keeps its second factor. The settings exist to let one named
// role through, and an unknown one is not a request anybody made.
TEST_F(SettingsTest, UnknownRoleIsProtected)
{
    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorEnabledFor(proto::router::SESSION_TYPE_UNKNOWN));
    EXPECT_TRUE(settings.isTwoFactorEnabledFor(9999));
}

//--------------------------------------------------------------------------------------------------
// What the router writes has to be what it reads back, otherwise an administrator who switches a
// role off through the settings object ends up with a file that does not do what it says.
TEST_F(SettingsTest, WrittenValuesAreReadBack)
{
    {
        Settings settings;
        settings.setTwoFactorForAdmin(false);
        settings.setTwoFactorForManager(true);
        settings.setTwoFactorForOperator(false);
        ASSERT_TRUE(settings.sync());
    }

    const Settings reread;

    EXPECT_FALSE(reread.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(reread.isTwoFactorForManagerEnabled());
    EXPECT_FALSE(reread.isTwoFactorForOperatorEnabled());
}

//--------------------------------------------------------------------------------------------------
// A fresh configuration carries the protected values, so the file a router writes on its own says
// that the second factor is on rather than leaving the question open.
TEST_F(SettingsTest, FreshConfigurationProtectsEveryRole)
{
    {
        Settings settings;
        settings.reset();
        ASSERT_TRUE(settings.sync());
    }

    const Settings reread;

    EXPECT_TRUE(reread.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(reread.isTwoFactorForManagerEnabled());
    EXPECT_TRUE(reread.isTwoFactorForOperatorEnabled());
}