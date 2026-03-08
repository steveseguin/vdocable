#include "router/ui/main_window.h"

#include "router/ui/route_widget.h"
#include "router/util/router_text.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <map>

namespace router::ui {
namespace {

QString sourcePrimaryLabel(const router::app::SourceInfo &source) {
    return QString::fromStdString(router::util::preferredDisplayName(source));
}

QString sourceListText(const router::app::SourceInfo &source) {
    const QString primary = sourcePrimaryLabel(source);
    const QString executable = QString::fromStdString(router::util::trimCopy(source.executableName));
    return QString("%1\n%2  PID %3")
        .arg(primary)
        .arg(executable.isEmpty() ? QStringLiteral("Audio session") : executable)
        .arg(source.processId);
}

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setupUi();
    applyTheme();
    loadSettings();
    refreshSources();

    routeTimer_ = new QTimer(this);
    routeTimer_->setInterval(250);
    connect(routeTimer_, &QTimer::timeout, this, &MainWindow::updateRoutes);
    routeTimer_->start();

    sourceRefreshTimer_ = new QTimer(this);
    sourceRefreshTimer_->setInterval(4000);
    connect(sourceRefreshTimer_, &QTimer::timeout, this, &MainWindow::refreshSources);
    sourceRefreshTimer_->start();
}

MainWindow::~MainWindow() {
    saveSettings();
    for (auto &route : routes_) {
        if (route && route->publisher) {
            route->publisher->stop();
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::setupUi() {
    setWindowTitle("VDO Cable");
    resize(1360, 860);

    auto *central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(18);

    auto *leftPanel = new QVBoxLayout();
    leftPanel->setSpacing(12);

    auto *sourceHeader = new QLabel("Sources", central);
    sourceHeader->setObjectName("panelTitle");
    leftPanel->addWidget(sourceHeader);

    auto *sourceHint = new QLabel(
        "Active Windows audio sessions. Add one route per application you want to publish to its own VDO.Ninja stream.",
        central);
    sourceHint->setWordWrap(true);
    sourceHint->setObjectName("panelHint");
    leftPanel->addWidget(sourceHint);

    sourceList_ = new QListWidget(central);
    sourceList_->setSelectionMode(QAbstractItemView::SingleSelection);
    sourceList_->setMinimumWidth(340);
    sourceList_->setMinimumHeight(420);
    leftPanel->addWidget(sourceList_, 1);

    auto *sourceButtonRow = new QHBoxLayout();
    refreshSourcesButton_ = new QPushButton("Refresh", central);
    addRouteButton_ = new QPushButton("Add Route", central);
    addRouteButton_->setEnabled(false);
    sourceButtonRow->addWidget(refreshSourcesButton_);
    sourceButtonRow->addWidget(addRouteButton_);
    leftPanel->addLayout(sourceButtonRow);

    auto *settingsBox = new QGroupBox("Publisher Defaults", central);
    auto *settingsLayout = new QGridLayout(settingsBox);
    serverInput_ = new QLineEdit("wss://wss.vdo.ninja", settingsBox);
    saltInput_ = new QLineEdit("vdo.ninja", settingsBox);
    viewerLimitSpin_ = new QSpinBox(settingsBox);
    viewerLimitSpin_->setRange(1, 64);
    viewerLimitSpin_->setValue(8);
    settingsLayout->addWidget(new QLabel("WebSocket", settingsBox), 0, 0);
    settingsLayout->addWidget(serverInput_, 0, 1);
    settingsLayout->addWidget(new QLabel("Salt", settingsBox), 1, 0);
    settingsLayout->addWidget(saltInput_, 1, 1);
    settingsLayout->addWidget(new QLabel("Max Viewers", settingsBox), 2, 0);
    settingsLayout->addWidget(viewerLimitSpin_, 2, 1);
    leftPanel->addWidget(settingsBox);

    rootLayout->addLayout(leftPanel, 0);

    auto *rightPanel = new QVBoxLayout();
    rightPanel->setSpacing(12);

    auto *routesHeader = new QLabel("Routes", central);
    routesHeader->setObjectName("panelTitle");
    rightPanel->addWidget(routesHeader);

    auto *routesHint = new QLabel(
        "Each route uses its own process capture, websocket signaling connection, and single-track WebRTC audio publisher.",
        central);
    routesHint->setWordWrap(true);
    routesHint->setObjectName("panelHint");
    rightPanel->addWidget(routesHint);

    auto *bulkRow = new QHBoxLayout();
    startAllButton_ = new QPushButton("Start All", central);
    stopAllButton_ = new QPushButton("Stop All", central);
    bulkRow->addWidget(startAllButton_);
    bulkRow->addWidget(stopAllButton_);
    bulkRow->addStretch();
    rightPanel->addLayout(bulkRow);

    auto *scrollArea = new QScrollArea(central);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    routeContainer_ = new QWidget(scrollArea);
    routeLayout_ = new QVBoxLayout(routeContainer_);
    routeLayout_->setContentsMargins(0, 0, 0, 0);
    routeLayout_->setSpacing(12);
    routeLayout_->addStretch();
    scrollArea->setWidget(routeContainer_);
    rightPanel->addWidget(scrollArea, 1);

    rootLayout->addLayout(rightPanel, 1);
    setCentralWidget(central);

    connect(refreshSourcesButton_, &QPushButton::clicked, this, &MainWindow::refreshSources);
    connect(addRouteButton_, &QPushButton::clicked, this, &MainWindow::addRouteFromSelection);
    connect(sourceList_, &QListWidget::itemSelectionChanged, this, [this]() {
        addRouteButton_->setEnabled(sourceList_->currentRow() >= 0);
    });
    connect(sourceList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        addRouteFromSelection();
    });
    connect(startAllButton_, &QPushButton::clicked, this, [this]() {
        for (auto &route : routes_) {
            if (route) {
                startRoute(*route);
            }
        }
    });
    connect(stopAllButton_, &QPushButton::clicked, this, [this]() {
        for (auto &route : routes_) {
            if (route) {
                stopRoute(*route);
            }
        }
    });
    connect(serverInput_, &QLineEdit::textChanged, this, [this]() {
        saveSettings();
    });
    connect(saltInput_, &QLineEdit::textChanged, this, [this]() {
        updateRouteConflictIndicators();
        saveSettings();
    });
    connect(viewerLimitSpin_,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            [this](int) {
                saveSettings();
            });
}

void MainWindow::applyTheme() {
    setStyleSheet(R"(
        QWidget {
            background: #111418;
            color: #f1eee7;
            font-size: 12px;
        }
        QMainWindow {
            background: #111418;
        }
        QLabel#panelTitle {
            font-size: 22px;
            font-weight: 700;
            color: #f6f0df;
        }
        QLabel#panelHint {
            color: #b8b09b;
        }
        QListWidget, QLineEdit, QSpinBox, QGroupBox {
            background: #191d22;
            border: 1px solid #2b323a;
            border-radius: 8px;
        }
        QLineEdit[error="true"] {
            border: 1px solid #cf5f58;
            background: #2a1b1a;
            color: #ffe7e4;
        }
        QListWidget::item {
            padding: 8px;
            border-radius: 6px;
        }
        QListWidget::item:selected {
            background: #d97a2b;
            color: #161616;
        }
        QPushButton {
            background: #d97a2b;
            color: #161616;
            border: none;
            border-radius: 8px;
            padding: 8px 14px;
            font-weight: 700;
        }
        QPushButton:disabled {
            background: #47515c;
            color: #a8afb7;
        }
        QPushButton:hover:!disabled {
            background: #ef8d3d;
        }
        QGroupBox {
            margin-top: 8px;
            padding: 14px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
            padding: 0 4px;
        }
        QFrame#routeCard {
            background: #171b20;
            border: 1px solid #2e363f;
            border-radius: 12px;
        }
        QLabel#routeSource {
            font-size: 15px;
            font-weight: 700;
            color: #f6f0df;
        }
        QLabel#routeStatus {
            padding: 6px 10px;
            border-radius: 999px;
            background: #28313a;
            color: #d8e0ea;
        }
        QLabel#routeStatus[error="true"] {
            background: #512729;
            color: #ffd3d0;
        }
        QProgressBar {
            border: 1px solid #303841;
            border-radius: 8px;
            background: #111418;
            text-align: center;
            min-height: 22px;
        }
        QProgressBar::chunk {
            background: #d97a2b;
            border-radius: 7px;
        }
    )");
}

void MainWindow::refreshSources() {
    sources_ = router::app::AudioRoutePublisher::listAudioSources();
    sourceList_->clear();
    for (const auto &source : sources_) {
        auto *item = new QListWidgetItem(sourceListText(source));
        sourceList_->addItem(item);
    }

    std::map<uint32_t, router::app::SourceInfo> sourceMap;
    for (const auto &source : sources_) {
        sourceMap[source.processId] = source;
    }

    for (auto &route : routes_) {
        if (!route) {
            continue;
        }
        const auto it = sourceMap.find(route->source.processId);
        if (it != sourceMap.end()) {
            route->source = it->second;
        }
        route->widget->setSourceLabel(QString::fromStdString(router::app::AudioRoutePublisher::formatSourceLabel(route->source)));
    }
    updateRouteConflictIndicators();
}

void MainWindow::addRouteFromSelection() {
    const int row = sourceList_->currentRow();
    if (row < 0 || row >= static_cast<int>(sources_.size())) {
        return;
    }

    auto entry = std::make_unique<RouteEntry>();
    entry->source = sources_[static_cast<size_t>(row)];
    entry->publisher = std::make_unique<router::app::AudioRoutePublisher>();
    entry->widget = new RouteWidget(routeContainer_);
    entry->widget->setSourceLabel(QString::fromStdString(router::app::AudioRoutePublisher::formatSourceLabel(entry->source)));
    entry->widget->setLabelText(sourcePrimaryLabel(entry->source));
    entry->widget->setStreamId(makeDefaultStreamId(entry->source));

    auto *entryPtr = entry.get();
    connect(entry->widget, &RouteWidget::startRequested, this, [this, entryPtr]() {
        startRoute(*entryPtr);
    });
    connect(entry->widget, &RouteWidget::stopRequested, this, [this, entryPtr]() {
        stopRoute(*entryPtr);
    });
    connect(entry->widget, &RouteWidget::removeRequested, this, [this, entryPtr]() {
        removeRoute(entryPtr);
    });
    connect(entry->widget, &RouteWidget::configurationChanged, this, [this]() {
        updateRouteConflictIndicators();
        saveSettings();
    });

    routeLayout_->insertWidget(routeLayout_->count() - 1, entry->widget);
    routes_.push_back(std::move(entry));
    updateRouteConflictIndicators();
    saveSettings();
}

void MainWindow::startRoute(RouteEntry &route) {
    router::app::StartOptions options;
    options.streamId = router::util::sanitizeRouteId(route.widget->streamId().toStdString(), 64);
    if (options.streamId.empty()) {
        options.streamId = makeDefaultStreamId(route.source).toStdString();
        route.widget->setStreamId(QString::fromStdString(options.streamId));
    }
    options.room = route.widget->room().trimmed().toStdString();
    options.password = route.widget->password().trimmed().toStdString();
    options.label = route.widget->label().trimmed().toStdString();
    if (options.label.empty()) {
        options.label = sourcePrimaryLabel(route.source).toStdString();
        route.widget->setLabelText(QString::fromStdString(options.label));
    }
    options.server = serverInput_->text().trimmed().isEmpty()
        ? "wss://wss.vdo.ninja"
        : serverInput_->text().trimmed().toStdString();
    options.salt = saltInput_->text().trimmed().isEmpty()
        ? "vdo.ninja"
        : saltInput_->text().trimmed().toStdString();
    options.maxViewers = viewerLimitSpin_->value();
    updateRouteConflictIndicators();

    const QString publishKey = routePublishKey(route);
    if (!publishKey.isEmpty()) {
        const int duplicates = static_cast<int>(std::count_if(routes_.begin(), routes_.end(), [this, &route, &publishKey](const auto &entry) {
            return entry && entry.get() != &route && routePublishKey(*entry) == publishKey;
        }));
        if (duplicates > 0) {
            const QString message =
                QString("Another route already resolves to the same published Stream ID (%1). Change the Stream ID or password before starting.")
                    .arg(publishKey);
            route.widget->setStatusText(message, true);
            route.widget->setStreamIdConflict(true, message);
            QMessageBox::warning(this, "Duplicate Stream ID", message);
            return;
        }
    }

    route.widget->setStatusText("Starting...", false);
    const bool started = route.publisher->start(route.source, options);
    route.widget->setLive(started);
    route.widget->setStatusText(QString::fromStdString(route.publisher->status()),
                                !route.publisher->lastError().empty() && !route.publisher->isLive());
    route.widget->setViewUrl(QString::fromStdString(route.publisher->shareLink()));
    route.lastObservedError = QString::fromStdString(route.publisher->lastError());
    saveSettings();
}

void MainWindow::stopRoute(RouteEntry &route) {
    route.publisher->stop();
    route.widget->setLive(false);
    route.widget->setViewUrl(QString());
    route.widget->setStatusText("Stopped", false);
    route.lastObservedError.clear();
    updateRouteConflictIndicators();
}

void MainWindow::removeRoute(RouteEntry *route) {
    const auto it = std::find_if(routes_.begin(), routes_.end(), [route](const auto &entry) {
        return entry.get() == route;
    });
    if (it == routes_.end()) {
        return;
    }

    (*it)->publisher->stop();
    routeLayout_->removeWidget((*it)->widget);
    (*it)->widget->deleteLater();
    routes_.erase(it);
    updateRouteConflictIndicators();
    saveSettings();
}

void MainWindow::updateRoutes() {
    for (auto &route : routes_) {
        if (!route) {
            continue;
        }
        route->widget->setLive(route->publisher->isLive());
        const QString errorText = QString::fromStdString(route->publisher->lastError());
        route->widget->setStatusText(QString::fromStdString(route->publisher->status()),
                                     !errorText.isEmpty() && !route->publisher->isLive());
        route->widget->setViewUrl(QString::fromStdString(route->publisher->shareLink()));
        route->widget->setMeter(route->publisher->audioLevelRms(), route->publisher->audioPeak());
        if (!errorText.isEmpty() && errorText != route->lastObservedError &&
            router::util::isStreamIdInUseAlert(errorText.toStdString())) {
            QMessageBox::warning(this,
                                 "Stream ID In Use",
                                 QString("%1\n\nThe route was stopped to avoid conflicting publishes.").arg(errorText));
        }
        route->lastObservedError = errorText;
    }
    updateRouteConflictIndicators();
}

void MainWindow::loadSettings() {
    QSettings settings("VDO.Ninja", "VDOCable");
    serverInput_->setText(settings.value("server", "wss://wss.vdo.ninja").toString());
    saltInput_->setText(settings.value("salt", "vdo.ninja").toString());
    viewerLimitSpin_->setValue(settings.value("maxViewers", 8).toInt());

    const int routeCount = settings.beginReadArray("routes");
    for (int i = 0; i < routeCount; ++i) {
        settings.setArrayIndex(i);
        router::app::SourceInfo source;
        source.processId = settings.value("processId").toUInt();
        source.displayName = settings.value("displayName").toString().toStdString();
        source.executableName = settings.value("executableName").toString().toStdString();

        auto entry = std::make_unique<RouteEntry>();
        entry->source = source;
        entry->publisher = std::make_unique<router::app::AudioRoutePublisher>();
        entry->widget = new RouteWidget(routeContainer_);
        entry->widget->setSourceLabel(QString::fromStdString(router::app::AudioRoutePublisher::formatSourceLabel(entry->source)));
        entry->widget->setStreamId(settings.value("streamId").toString());
        entry->widget->setRoom(settings.value("room").toString());
        entry->widget->setPassword(settings.value("password").toString());
        entry->widget->setLabelText(settings.value("label", sourcePrimaryLabel(entry->source)).toString());

        auto *entryPtr = entry.get();
        connect(entry->widget, &RouteWidget::startRequested, this, [this, entryPtr]() {
            startRoute(*entryPtr);
        });
        connect(entry->widget, &RouteWidget::stopRequested, this, [this, entryPtr]() {
            stopRoute(*entryPtr);
        });
        connect(entry->widget, &RouteWidget::removeRequested, this, [this, entryPtr]() {
            removeRoute(entryPtr);
        });
        connect(entry->widget, &RouteWidget::configurationChanged, this, [this]() {
            updateRouteConflictIndicators();
            saveSettings();
        });

        routeLayout_->insertWidget(routeLayout_->count() - 1, entry->widget);
        routes_.push_back(std::move(entry));
    }
    settings.endArray();
    updateRouteConflictIndicators();
}

void MainWindow::saveSettings() const {
    QSettings settings("VDO.Ninja", "VDOCable");
    settings.setValue("server", serverInput_->text().trimmed());
    settings.setValue("salt", saltInput_->text().trimmed());
    settings.setValue("maxViewers", viewerLimitSpin_->value());

    settings.beginWriteArray("routes");
    for (int i = 0; i < static_cast<int>(routes_.size()); ++i) {
        settings.setArrayIndex(i);
        const auto &route = routes_[static_cast<size_t>(i)];
        settings.setValue("processId", route->source.processId);
        settings.setValue("displayName", QString::fromStdString(route->source.displayName));
        settings.setValue("executableName", QString::fromStdString(route->source.executableName));
        settings.setValue("streamId", route->widget->streamId().trimmed());
        settings.setValue("room", route->widget->room().trimmed());
        settings.setValue("password", route->widget->password());
        settings.setValue("label", route->widget->label().trimmed());
    }
    settings.endArray();
}

QString MainWindow::makeDefaultStreamId(const router::app::SourceInfo &source) const {
    return QString::fromStdString(
        router::util::makeDefaultStreamId(source, static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch())));
}

QString MainWindow::routePublishKey(const RouteEntry &route) const {
    const std::string sanitized =
        router::util::sanitizeRouteId(route.widget->streamId().trimmed().toStdString(), 64);
    if (sanitized.empty()) {
        return QString();
    }
    const std::string salt = saltInput_->text().trimmed().isEmpty()
        ? "vdo.ninja"
        : saltInput_->text().trimmed().toStdString();
    return QString::fromStdString(router::util::effectivePublishedStreamId(
        sanitized,
        route.widget->password().trimmed().toStdString(),
        salt));
}

void MainWindow::updateRouteConflictIndicators() {
    std::map<QString, int> publishKeys;
    for (const auto &route : routes_) {
        if (!route) {
            continue;
        }
        const QString key = routePublishKey(*route);
        if (!key.isEmpty()) {
            publishKeys[key] += 1;
        }
    }

    for (const auto &route : routes_) {
        if (!route) {
            continue;
        }
        const QString key = routePublishKey(*route);
        const bool conflict = !key.isEmpty() && publishKeys[key] > 1;
        const QString tooltip = conflict
            ? QString("Another route in this app resolves to the same published Stream ID: %1").arg(key)
            : QString();
        route->widget->setStreamIdConflict(conflict, tooltip);
    }
}

}  // namespace router::ui
