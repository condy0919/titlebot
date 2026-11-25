#include "ircbot/registration.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using ircbot::Registration;
using ircbot::RegistrationConfig;
using ircbot::RegistrationError;
using ircbot::SaslConfig;
using ircbot::SaslMechanism;
using Lines = std::vector<std::string>;

auto Feed(Registration& registration, std::string_view line) {
    auto message = ircbot::ParseMessage(line);
    EXPECT_TRUE(message.has_value());
    return registration.Handle(*message);
}

Registration Plain(bool required = true, std::string password = "secret") {
    RegistrationConfig config;
    config.sasl = SaslConfig{SaslMechanism::kPlain, "alice", std::move(password), "", required};
    return Registration(std::move(config));
}

TEST(Registration, WaitsForLastLsAndLastAcknowledgement) {
    Registration registration;
    EXPECT_EQ(registration.Start(), Lines{"CAP LS 302"});
    EXPECT_TRUE(registration.Start().empty());
    EXPECT_EQ(Feed(registration, "CAP * LS * :message-tags"), Lines{});
    EXPECT_EQ(Feed(registration, "CAP * LS :server-time  "),
              Lines{"CAP REQ :message-tags server-time"});
    EXPECT_FALSE(registration.done());
    EXPECT_TRUE(registration.enabled().contains("cap-notify"));
    EXPECT_EQ(Feed(registration, "CAP bot ACK * :message-tags"), Lines{});
    EXPECT_FALSE(registration.enabled().contains("message-tags"));
    EXPECT_EQ(Feed(registration, "CAP bot ACK :server-time"), Lines{"CAP END"});
    EXPECT_TRUE(registration.done());
    EXPECT_TRUE(registration.enabled().contains("message-tags"));
    EXPECT_TRUE(registration.enabled().contains("server-time"));
    EXPECT_EQ(Feed(registration, ":server 001 bot :Welcome"), Lines{});
    EXPECT_TRUE(registration.enabled().contains("server-time"));
}

TEST(Registration, SettlesEveryRequestBeforeEndingNegotiation) {
    RegistrationConfig config;
    config.caps = {std::string(220, 'a'), std::string(220, 'b')};
    Registration registration(config);
    registration.Start();
    EXPECT_EQ(Feed(registration, "CAP * LS :" + config.caps[0] + " " + config.caps[1]),
              (Lines{"CAP REQ :" + config.caps[0], "CAP REQ :" + config.caps[1]}));
    EXPECT_EQ(Feed(registration, "CAP bot ACK :" + config.caps[0]), Lines{});
    EXPECT_FALSE(registration.done());
    EXPECT_EQ(Feed(registration, "CAP bot NAK :" + config.caps[1]), Lines{"CAP END"});
    EXPECT_TRUE(registration.done());
    EXPECT_TRUE(registration.enabled().contains(config.caps[0]));
    EXPECT_FALSE(registration.enabled().contains(config.caps[1]));
}

TEST(Registration, FailedOptionalSaslWaitsForOtherCapabilityRequests) {
    auto registration = Plain(false);
    registration.Start();
    EXPECT_EQ(Feed(registration, "CAP * LS :message-tags sasl"),
              Lines{"CAP REQ :message-tags sasl"});
    EXPECT_EQ(Feed(registration, "CAP bot ACK :sasl"), Lines{"AUTHENTICATE PLAIN"});
    EXPECT_EQ(Feed(registration, ":s 904 bot :Failed"), Lines{});
    EXPECT_FALSE(registration.done());
    EXPECT_EQ(Feed(registration, "CAP bot ACK :message-tags"), Lines{"CAP END"});
}

TEST(Registration, ListsCapabilitiesAtomicallyAndProcessesNewAndDel) {
    Registration registration;
    registration.Start();
    EXPECT_EQ(Feed(registration, "CAP * LS :"), Lines{"CAP END"});
    EXPECT_EQ(Feed(registration, "CAP bot NEW :server-time batch"), Lines{"CAP REQ :server-time"});
    EXPECT_EQ(Feed(registration, "CAP bot ACK :server-time"), Lines{});
    EXPECT_EQ(Feed(registration, "CAP bot LIST * :message-tags"), Lines{});
    EXPECT_TRUE(registration.enabled().contains("server-time"));
    EXPECT_FALSE(registration.enabled().contains("message-tags"));
    EXPECT_EQ(Feed(registration, "CAP bot LIST :batch"), Lines{});
    EXPECT_FALSE(registration.enabled().contains("server-time"));
    EXPECT_TRUE(registration.enabled().contains("message-tags"));
    EXPECT_TRUE(registration.enabled().contains("batch"));
    EXPECT_TRUE(registration.enabled().contains("cap-notify"));
    EXPECT_EQ(Feed(registration, "CAP bot DEL :message-tags batch"), Lines{});
    EXPECT_FALSE(registration.enabled().contains("message-tags"));
    EXPECT_FALSE(registration.enabled().contains("batch"));
}

TEST(Registration, OptionalCapsCanBeRejectedAndNamesAreCaseSensitive) {
    Registration registration;
    registration.Start();
    EXPECT_EQ(Feed(registration, "CAP * LS :message-tags SERVER-TIME"),
              Lines{"CAP REQ :message-tags"});
    EXPECT_EQ(Feed(registration, "CAP bot NAK :message-tags"), Lines{"CAP END"});
    EXPECT_FALSE(registration.enabled().contains("message-tags"));
    EXPECT_TRUE(registration.done());
}

TEST(Registration, SupportsServersWithoutCapButHonorsRequiredAuthentication) {
    for (auto reply : {":s 421 bot CAP :Unknown command", ":s 001 bot :Welcome"}) {
        Registration legacy;
        legacy.Start();
        EXPECT_EQ(Feed(legacy, reply), Lines{});
        EXPECT_TRUE(legacy.done());
        EXPECT_TRUE(legacy.enabled().empty());
        auto required = Plain();
        required.Start();
        EXPECT_EQ(Feed(required, reply), std::unexpected(RegistrationError::kSaslUnavailable));
        EXPECT_FALSE(required.done());
        auto optional = Plain(false);
        optional.Start();
        EXPECT_EQ(Feed(optional, reply), Lines{});
        EXPECT_TRUE(optional.done());
    }
}

TEST(Registration, SendsPlainCredentialsOnlyAfterAcknowledgementAndChallenge) {
    auto registration = Plain();
    registration.Start();
    EXPECT_EQ(Feed(registration, "AUTHENTICATE +"), Lines{});
    EXPECT_EQ(Feed(registration, "CAP * LS :sasl=EXTERNAL,PLAIN"), Lines{"CAP REQ :sasl"});
    EXPECT_EQ(Feed(registration, "AUTHENTICATE +"), Lines{});
    EXPECT_EQ(Feed(registration, "CAP bot ACK :sasl"), Lines{"AUTHENTICATE PLAIN"});
    EXPECT_EQ(Feed(registration, "AUTHENTICATE +"), Lines{"AUTHENTICATE AGFsaWNlAHNlY3JldA=="});
    EXPECT_EQ(Feed(registration, ":s 900 bot bot!u@h alice :Logged in"), Lines{});
    EXPECT_FALSE(registration.done());
    EXPECT_FALSE(registration.authenticated());
    EXPECT_EQ(Feed(registration, ":s 903 bot :Success"), Lines{"CAP END"});
    EXPECT_TRUE(registration.done());
    EXPECT_TRUE(registration.authenticated());
    EXPECT_EQ(Feed(registration, "AUTHENTICATE +"), Lines{});
    EXPECT_EQ(Feed(registration, ":s 901 bot bot!u@h :Logged out"), Lines{});
    EXPECT_FALSE(registration.authenticated());
}

TEST(Registration, SupportsExternalAndAuthorizationIdentity) {
    for (auto identity : {"", "alice"}) {
        RegistrationConfig config;
        config.sasl = SaslConfig{SaslMechanism::kExternal, "", "", identity, true};
        Registration registration(std::move(config));
        registration.Start();
        EXPECT_EQ(Feed(registration, "CAP * LS :sasl"), Lines{"CAP REQ :sasl"});
        EXPECT_EQ(Feed(registration, "CAP bot ACK :sasl"), Lines{"AUTHENTICATE EXTERNAL"});
        EXPECT_EQ(Feed(registration, "AUTHENTICATE +"),
                  Lines{identity[0] ? "AUTHENTICATE YWxpY2U=" : "AUTHENTICATE +"});
        EXPECT_EQ(Feed(registration, ":s 903 bot :Success"), Lines{"CAP END"});
    }
}

TEST(Registration, SplitsBase64At400BytesAndTerminatesExactMultiples) {
    for (std::size_t password_size : {293u, 294u, 593u}) {
        // Empty authzid, five username bytes and two separators add seven bytes before Base64.
        auto registration = Plain(true, std::string(password_size, 'p'));
        registration.Start();
        ASSERT_TRUE(Feed(registration, "CAP * LS :sasl"));
        ASSERT_TRUE(Feed(registration, "CAP bot ACK :sasl"));
        auto output = Feed(registration, "AUTHENTICATE +");
        ASSERT_TRUE(output);
        const auto encoded_size = (password_size + 7 + 2) / 3 * 4;
        EXPECT_EQ(output->size(), encoded_size / 400 + 1);
        for (std::size_t i = 0; i + 1 < output->size(); ++i) {
            EXPECT_EQ((*output)[i].size(), std::string_view("AUTHENTICATE ").size() + 400);
        }
        if (encoded_size % 400 == 0) {
            EXPECT_EQ(output->back(), "AUTHENTICATE +");
        } else {
            EXPECT_EQ(output->back().size(), std::string_view("AUTHENTICATE ").size() + 4);
        }
    }
}

TEST(Registration, ChecksAdvertisedMechanismsAndUsesLastCapabilityValue) {
    for (auto caps : {"sasl=EXTERNAL", "sasl=PLAIN sasl=EXTERNAL", "message-tags"}) {
        auto registration = Plain();
        registration.Start();
        EXPECT_EQ(Feed(registration, "CAP * LS :" + std::string(caps)),
                  std::unexpected(RegistrationError::kSaslUnavailable));
    }
    auto optional = Plain(false);
    optional.Start();
    EXPECT_EQ(Feed(optional, "CAP * LS :sasl=EXTERNAL"), Lines{"CAP END"});
}

TEST(Registration, DistinguishesRequiredAndOptionalAuthenticationFailure) {
    for (auto numeric : {"902", "904", "905", "906", "907"}) {
        for (bool required : {true, false}) {
            auto registration = Plain(required);
            registration.Start();
            ASSERT_TRUE(Feed(registration, "CAP * LS :sasl"));
            ASSERT_TRUE(Feed(registration, "CAP bot ACK :sasl"));
            const auto result = Feed(registration, ":s " + std::string(numeric) + " bot :Failed");
            if (required) {
                EXPECT_EQ(result, std::unexpected(RegistrationError::kSaslFailed));
                EXPECT_FALSE(registration.done());
            } else {
                EXPECT_EQ(result, Lines{"CAP END"});
                EXPECT_TRUE(registration.done());
            }
            EXPECT_FALSE(registration.authenticated());
        }
    }
}

TEST(Registration, RejectedOrRemovedSaslCannotBypassRequiredAuthentication) {
    for (auto command : {"NAK", "DEL", "ACK -"}) {
        auto registration = Plain();
        registration.Start();
        ASSERT_TRUE(Feed(registration, "CAP * LS :sasl"));
        const auto reply = std::string(command) == "ACK -"
                               ? "CAP bot ACK :-sasl"
                               : "CAP bot " + std::string(command) + " :sasl";
        EXPECT_EQ(Feed(registration, reply), std::unexpected(RegistrationError::kSaslRejected));
    }
}

TEST(Registration, UnexpectedChallengesAbortOptionalAuthentication) {
    auto registration = Plain(false);
    registration.Start();
    ASSERT_TRUE(Feed(registration, "CAP * LS :sasl"));
    ASSERT_TRUE(Feed(registration, "CAP bot ACK :sasl"));
    EXPECT_EQ(Feed(registration, "AUTHENTICATE YQ=="), (Lines{"AUTHENTICATE *", "CAP END"}));
    EXPECT_FALSE(registration.authenticated());
}

TEST(Registration, RejectsInvalidConfigurationAndMalformedCapMessages) {
    RegistrationConfig config;
    config.caps = {"message-tags\r\nQUIT :injected"};
    Registration invalid(std::move(config));
    invalid.Start();
    EXPECT_EQ(Feed(invalid, "CAP * LS :message-tags"),
              std::unexpected(RegistrationError::kInvalidConfig));
    Registration malformed;
    malformed.Start();
    EXPECT_EQ(Feed(malformed, "CAP * LS"), std::unexpected(RegistrationError::kProtocol));

    RegistrationConfig credentials;
    credentials.sasl =
        SaslConfig{SaslMechanism::kPlain, std::string("a\0b", 3), "secret", "", true};
    EXPECT_EQ(ircbot::ValidateRegistration(credentials),
              std::unexpected(RegistrationError::kInvalidConfig));
    credentials.sasl->username = "alice";
    EXPECT_TRUE(ircbot::ValidateRegistration(credentials));
}

TEST(Registration, NewCapabilityDoesNotRetryAnUnrelatedRejectedRequest) {
    Registration registration;
    registration.Start();
    ASSERT_EQ(Feed(registration, "CAP * LS :message-tags"), Lines{"CAP REQ :message-tags"});
    ASSERT_EQ(Feed(registration, "CAP bot NAK :message-tags"), Lines{"CAP END"});
    EXPECT_EQ(Feed(registration, "CAP bot NEW :batch"), Lines{});
    EXPECT_EQ(Feed(registration, "CAP bot NEW :message-tags"), Lines{"CAP REQ :message-tags"});
}

TEST(Registration, RecordsMechanismsListedByServer) {
    Registration registration;
    registration.Start();
    const auto line =
        ircbot::ParseMessage(":irc 908 bot PLAIN,EXTERNAL :available SASL mechanisms");
    ASSERT_TRUE(line);
    EXPECT_TRUE(registration.Handle(*line));
    EXPECT_EQ(registration.sasl_mechanisms(), "PLAIN,EXTERNAL");
}

TEST(Registration, ListingSaslAsEnabledDoesNotSkipAuthentication) {
    auto registration = Plain();
    registration.Start();
    ASSERT_TRUE(Feed(registration, "CAP bot LIST :sasl"));
    EXPECT_EQ(Feed(registration, "CAP * LS :sasl=PLAIN"), Lines{"CAP REQ :sasl"});
    EXPECT_FALSE(registration.done());
    EXPECT_FALSE(registration.authenticated());
    EXPECT_EQ(Feed(registration, "CAP bot ACK :sasl"), Lines{"AUTHENTICATE PLAIN"});
}

TEST(Registration, UnsolicitedAcknowledgementsNeverStartAuthentication) {
    auto registration = Plain();
    registration.Start();
    EXPECT_EQ(Feed(registration, "CAP bot ACK :sasl"), Lines{});
    EXPECT_EQ(Feed(registration, "AUTHENTICATE +"), Lines{});
    EXPECT_FALSE(registration.authenticated());
}

TEST(Registration, MalformedSuccessNumericCannotAuthenticate) {
    auto registration = Plain();
    registration.Start();
    ASSERT_TRUE(Feed(registration, "CAP * LS :sasl"));
    ASSERT_TRUE(Feed(registration, "CAP bot ACK :sasl"));
    ASSERT_TRUE(Feed(registration, "AUTHENTICATE +"));
    EXPECT_EQ(Feed(registration, "903"), std::unexpected(RegistrationError::kProtocol));
    EXPECT_FALSE(registration.authenticated());
    EXPECT_FALSE(registration.done());
}

TEST(Registration, AccountLossBeforeWelcomeCannotBypassRequiredAuthentication) {
    auto registration = Plain();
    registration.Start();
    ASSERT_TRUE(Feed(registration, "CAP * LS :sasl"));
    ASSERT_TRUE(Feed(registration, "CAP bot ACK :sasl"));
    ASSERT_TRUE(Feed(registration, "AUTHENTICATE +"));
    ASSERT_EQ(Feed(registration, ":s 903 bot :Success"), Lines{"CAP END"});
    ASSERT_TRUE(Feed(registration, ":s 901 bot bot!u@h :Logged out"));
    EXPECT_EQ(Feed(registration, ":s 001 bot :Welcome"),
              std::unexpected(RegistrationError::kSaslUnavailable));
}

TEST(Registration, BoundsTheTotalSaslResponseBeforeBase64Expansion) {
    RegistrationConfig config;
    config.sasl = SaslConfig{SaslMechanism::kPlain, "alice", std::string(12281, 'p'), "", true};
    EXPECT_TRUE(ircbot::ValidateRegistration(config));
    config.sasl->password.push_back('p');
    EXPECT_EQ(ircbot::ValidateRegistration(config),
              std::unexpected(RegistrationError::kInvalidConfig));
    config.sasl = SaslConfig{SaslMechanism::kExternal, "", "", std::string(12288, 'a'), true};
    EXPECT_TRUE(ircbot::ValidateRegistration(config));
    config.sasl->authzid.push_back('a');
    EXPECT_EQ(ircbot::ValidateRegistration(config),
              std::unexpected(RegistrationError::kInvalidConfig));
}

TEST(Registration, RemovingSaslAvailabilityDoesNotUndoCompletedAuthentication) {
    auto registration = Plain();
    registration.Start();
    ASSERT_TRUE(Feed(registration, "CAP * LS :message-tags sasl"));
    ASSERT_TRUE(Feed(registration, "CAP bot ACK :sasl"));
    ASSERT_TRUE(Feed(registration, "AUTHENTICATE +"));
    ASSERT_EQ(Feed(registration, ":s 903 bot :Success"), Lines{});
    EXPECT_EQ(Feed(registration, "CAP bot DEL :sasl"), Lines{});
    EXPECT_TRUE(registration.authenticated());
    EXPECT_FALSE(registration.enabled().contains("sasl"));
    EXPECT_EQ(Feed(registration, "CAP bot ACK :message-tags"), Lines{"CAP END"});
}

TEST(Registration, LegacyWelcomeClearsUnfinishedAcknowledgements) {
    Registration registration;
    registration.Start();
    ASSERT_TRUE(Feed(registration, "CAP * LS :message-tags"));
    ASSERT_EQ(Feed(registration, "CAP bot ACK * :message-tags"), Lines{});
    ASSERT_EQ(Feed(registration, ":s 001 bot :Welcome"), Lines{});
    EXPECT_TRUE(registration.enabled().empty());
    EXPECT_EQ(Feed(registration, "CAP bot LS :server-time"), Lines{"CAP REQ :server-time"});
    EXPECT_EQ(Feed(registration, "CAP bot ACK :server-time"), Lines{});
    EXPECT_FALSE(registration.enabled().contains("message-tags"));
    EXPECT_TRUE(registration.enabled().contains("server-time"));
}

TEST(Registration, BoundsMultilineCapabilityStorage) {
    for (auto command : {"LS", "LIST", "ACK"}) {
        Registration registration;
        registration.Start();
        for (unsigned i = 0; i < 512; ++i) {
            ASSERT_EQ(Feed(registration,
                           "CAP * " + std::string(command) + " * :cap-" + std::to_string(i)),
                      Lines{});
        }
        EXPECT_EQ(Feed(registration, "CAP * " + std::string(command) + " * :one-too-many"),
                  std::unexpected(RegistrationError::kLimit));
    }
}
} // namespace
