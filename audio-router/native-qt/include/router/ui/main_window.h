#pragma once

#include <QMainWindow>

#include <memory>
#include <vector>

#include "router/app/audio_route_publisher.h"

class QListWidget;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QVBoxLayout;

namespace router::ui {

class RouteWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    struct RouteEntry {
        router::app::SourceInfo source;
        std::unique_ptr<router::app::AudioRoutePublisher> publisher;
        RouteWidget *widget = nullptr;
        QString lastObservedError;
    };

    void setupUi();
    void applyTheme();
    void refreshSources();
    void addRouteFromSelection();
    void startRoute(RouteEntry &route);
    void stopRoute(RouteEntry &route);
    void removeRoute(RouteEntry *route);
    void updateRoutes();
    void updateRouteConflictIndicators();
    void loadSettings();
    void saveSettings() const;
    QString makeDefaultStreamId(const router::app::SourceInfo &source) const;
    QString routePublishKey(const RouteEntry &route) const;

    QListWidget *sourceList_ = nullptr;
    QPushButton *refreshSourcesButton_ = nullptr;
    QPushButton *addRouteButton_ = nullptr;
    QPushButton *startAllButton_ = nullptr;
    QPushButton *stopAllButton_ = nullptr;
    QLineEdit *serverInput_ = nullptr;
    QLineEdit *saltInput_ = nullptr;
    QSpinBox *viewerLimitSpin_ = nullptr;
    QWidget *routeContainer_ = nullptr;
    QVBoxLayout *routeLayout_ = nullptr;
    QTimer *routeTimer_ = nullptr;
    QTimer *sourceRefreshTimer_ = nullptr;
    std::vector<router::app::SourceInfo> sources_;
    std::vector<std::unique_ptr<RouteEntry>> routes_;
};

}  // namespace router::ui
