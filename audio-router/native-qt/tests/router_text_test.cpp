#include "router/util/router_text.h"
#include "router/ui/publisher_settings.h"

#include <QApplication>
#include <QTemporaryDir>

#include <array>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

bool hasOnlyAllowedChars(const std::string &value) {
    for (char ch : value) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) {
            return false;
        }
    }
    return true;
}


void testPublisherSettings() {
    struct Defaults {
        const char *server;
        const char *salt;
        int viewers;
    };
    const std::array<Defaults, 5> cases = {{
        {"wss://wss.vdo.ninja", "vdo.ninja", 8},
        {"wss://custom.example", "vdo.ninja", 8},
        {"wss://wss.vdo.ninja", "custom-salt", 8},
        {"wss://wss.vdo.ninja", "vdo.ninja", 12},
        {"wss://custom.example", "custom-salt", 12}
    }};
    for (const auto &defaults : cases) {
        QTemporaryDir directory;
        expect(directory.isValid(), "settings test needs a temporary directory");
        const QString filename = directory.filePath("settings.ini");
        QSettings settings(filename, QSettings::IniFormat);
        settings.setValue("server", defaults.server);
        settings.setValue("salt", defaults.salt);
        settings.setValue("maxViewers", defaults.viewers);
        settings.beginWriteArray("routes", 2);
        for (int i = 0; i < 2; ++i) {
            settings.setArrayIndex(i);
            settings.setValue("streamId", QString("saved_route_%1").arg(i));
        }
        settings.endArray();
        settings.sync();

        QLineEdit server("wss://wss.vdo.ninja");
        QLineEdit salt("vdo.ninja");
        QSpinBox viewers;
        viewers.setRange(1, 64);
        viewers.setValue(8);
        int saveCount = 0;
        int loadedRoutes = 0;
        const auto save = [&]() {
            ++saveCount;
            QSettings output(filename, QSettings::IniFormat);
            output.setValue("server", server.text());
            output.setValue("salt", salt.text());
            output.setValue("maxViewers", viewers.value());
            output.beginWriteArray("routes", loadedRoutes);
            for (int i = 0; i < loadedRoutes; ++i) {
                output.setArrayIndex(i);
                output.setValue("streamId", QString("saved_route_%1").arg(i));
            }
            output.endArray();
        };
        QObject::connect(&server, &QLineEdit::textChanged, save);
        QObject::connect(&salt, &QLineEdit::textChanged, save);
        QObject::connect(&viewers, qOverload<int>(&QSpinBox::valueChanged), save);

        router::ui::restorePublisherSettings(settings, server, salt, viewers);
        expect(saveCount == 0, "restoring publisher defaults must not save incomplete routes");
        expect(server.text() == defaults.server, "saved server must be restored");
        expect(salt.text() == defaults.salt, "saved salt must be restored");
        expect(viewers.value() == defaults.viewers, "saved viewer limit must be restored");
        loadedRoutes = settings.beginReadArray("routes");
        expect(loadedRoutes == 2, "all saved routes must survive restoring defaults");
        for (int i = 0; i < loadedRoutes; ++i) {
            settings.setArrayIndex(i);
            expect(settings.value("streamId").toString() == QString("saved_route_%1").arg(i),
                   "saved route contents must survive restoring defaults");
        }
        settings.endArray();

        server.setText("wss://edited.example");
        salt.setText("edited-salt");
        viewers.setValue(32);
        expect(saveCount == 3, "normal edits must resume persistence after restoration");
        expect(settings.value("server").toString() == "wss://edited.example", "server edits must persist");
        expect(settings.value("salt").toString() == "edited-salt", "salt edits must persist");
        expect(settings.value("maxViewers").toInt() == 32, "viewer limit edits must persist");
        expect(settings.beginReadArray("routes") == 2, "normal saves must retain loaded routes");
        settings.endArray();

        server.blockSignals(true);
        router::ui::restorePublisherSettings(settings, server, salt, viewers);
        expect(server.signalsBlocked(), "restoration must preserve preexisting signal blocking");
        expect(!salt.signalsBlocked() && !viewers.signalsBlocked(), "other controls must remain unblocked");
    }
}

}  // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    testPublisherSettings();
    router::app::SourceInfo source;
    source.processId = 4242;
    source.displayName = "Discord";
    source.executableName = "Discord.exe";

    expect(router::util::preferredDisplayName(source) == "Discord", "preferred display name should prefer displayName");
    expect(router::util::formatSourceLabel(source) == "Discord [Discord.exe] (PID 4242)", "source label should include pid");
    expect(router::util::sanitizeRouteId(" game-chat / mix ", 64) == "game_chat___mix", "sanitize should normalize separators");
    expect(router::util::sanitizeRouteId("abc123", 3) == "abc", "sanitize should honor max length");
    expect(router::util::makeDefaultStreamId(source, 1234567ULL) == "discord_234567", "default stream id should use lowercase basename and suffix");
    expect(!router::util::isEncryptionDisabled("password123"), "normal password should keep encryption enabled");
    expect(router::util::isEncryptionDisabled("false"), "false password should disable encryption");
    expect(router::util::effectivePublishPassword("") == "someEncryptionKey123", "empty password should map to default publish password");
    expect(router::util::effectivePublishedStreamId("mix_bus", "false", "vdo.ninja") == "mix_bus", "unencrypted stream id should stay unchanged");
    expect(router::util::effectivePublishedStreamId("mix_bus", "", "vdo.ninja") == "mix_bus808d64", "encrypted stream id should include deterministic suffix");
    expect(router::util::effectivePublishedStreamId("mix_bus", "roompass", "salt42") == "mix_bus0d7a91", "password and salt should alter effective stream id");
    expect(router::util::effectivePublishedStreamId("mix_bus", "roompass", "salt42") !=
               router::util::effectivePublishedStreamId("mix_bus", "roompass2", "salt42"),
           "different passwords should produce different effective stream ids");
    expect(router::util::effectivePublishedStreamId("mix bus", "", "vdo.ninja") ==
               router::util::effectivePublishedStreamId("mix_bus", "", "vdo.ninja"),
           "sanitized stream ids should resolve deterministically");
    expect(router::util::isStreamIdInUseAlert("alert: streamid-already-published"), "known stream collision alert should be detected");
    expect(router::util::isStreamIdInUseAlert("This stream ID is already in use"), "plain language collision alert should be detected");
    expect(router::util::isStreamIdInUseAlert("the room already has this streamid assigned"), "variant collision alert should be detected");
    expect(!router::util::isStreamIdInUseAlert("viewer connected"), "non-collision messages should not match");
    expect(router::util::reconnectDelayMs(1) == 2000, "first reconnect delay should be 2 seconds");
    expect(router::util::reconnectDelayMs(3) == 8000, "third reconnect delay should back off");
    expect(router::util::reconnectDelayMs(8) == 15000, "reconnect delay should cap at 15 seconds");
    expect(router::util::reconnectDelayMs(50) == 15000, "reconnect delay should stay capped");

    router::app::SourceInfo fallback;
    fallback.processId = 17;
    expect(router::util::preferredDisplayName(fallback) == "Process 17", "fallback label should use pid");
    expect(router::util::makeDefaultStreamId(fallback, 99ULL) == "audio_99", "fallback stream id should use audio prefix");

    const std::array<std::string, 5> fuzzInputs = {
        " stream id ",
        "mix/bus/1",
        "discord+spotify",
        "CAPS_and-lower",
        "1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ++++"
    };
    for (const auto &input : fuzzInputs) {
        const std::string sanitized = router::util::sanitizeRouteId(input, 64);
        expect(sanitized.size() <= 64, "sanitized route id should stay within max length");
        expect(hasOnlyAllowedChars(sanitized), "sanitized route id should contain only allowed characters");
        const std::string effective = router::util::effectivePublishedStreamId(input, "", "vdo.ninja");
        if (!sanitized.empty()) {
            expect(!effective.empty(), "effective published stream id should stay non-empty for valid inputs");
        }
    }

    return 0;
}
