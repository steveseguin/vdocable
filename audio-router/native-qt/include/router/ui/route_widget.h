#pragma once

#include <QFrame>

class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;

namespace router::ui {

class RouteWidget : public QFrame {
    Q_OBJECT

  public:
    explicit RouteWidget(QWidget *parent = nullptr);

    QString streamId() const;
    QString room() const;
    QString password() const;
    QString label() const;

    void setStreamId(const QString &value);
    void setRoom(const QString &value);
    void setPassword(const QString &value);
    void setLabelText(const QString &value);
    void setSourceLabel(const QString &value);
    void setStatusText(const QString &text, bool error);
    void setStreamIdConflict(bool conflict, const QString &tooltip = QString());
    void setViewUrl(const QString &url);
    void setMeter(float rms, float peak);
    void setLive(bool live);

  signals:
    void startRequested();
    void stopRequested();
    void removeRequested();
    void configurationChanged();

  private:
    void copyLink();
    void openLink();

    QLabel *sourceLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLineEdit *streamIdInput_ = nullptr;
    QLineEdit *roomInput_ = nullptr;
    QLineEdit *passwordInput_ = nullptr;
    QLineEdit *labelInput_ = nullptr;
    QLineEdit *viewUrlInput_ = nullptr;
    QProgressBar *meterBar_ = nullptr;
    QPushButton *copyLinkButton_ = nullptr;
    QPushButton *openLinkButton_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPushButton *removeButton_ = nullptr;
};

}  // namespace router::ui
