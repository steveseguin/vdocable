#pragma once

#include <QLineEdit>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>

namespace router::ui {

// Restoring controls must not invoke persistence while routes are still loading.
inline void restorePublisherSettings(QSettings &settings, QLineEdit &server,
                                     QLineEdit &salt, QSpinBox &viewerLimit) {
    const QSignalBlocker serverBlocker(server);
    const QSignalBlocker saltBlocker(salt);
    const QSignalBlocker viewerLimitBlocker(viewerLimit);
    server.setText(settings.value("server", "wss://wss.vdo.ninja").toString());
    salt.setText(settings.value("salt", "vdo.ninja").toString());
    viewerLimit.setValue(settings.value("maxViewers", 8).toInt());
}

}  // namespace router::ui
