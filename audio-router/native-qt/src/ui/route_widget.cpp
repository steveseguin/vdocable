#include "router/ui/route_widget.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace router::ui {

RouteWidget::RouteWidget(QWidget *parent)
    : QFrame(parent) {
    setObjectName("routeCard");
    setFrameShape(QFrame::StyledPanel);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 16, 16, 16);
    outerLayout->setSpacing(12);

    auto *headerLayout = new QHBoxLayout();
    sourceLabel_ = new QLabel(this);
    sourceLabel_->setObjectName("routeSource");
    statusLabel_ = new QLabel("Stopped", this);
    statusLabel_->setObjectName("routeStatus");
    headerLayout->addWidget(sourceLabel_, 1);
    headerLayout->addWidget(statusLabel_);
    outerLayout->addLayout(headerLayout);

    auto *formLayout = new QGridLayout();
    formLayout->setHorizontalSpacing(10);
    formLayout->setVerticalSpacing(8);

    streamIdInput_ = new QLineEdit(this);
    roomInput_ = new QLineEdit(this);
    passwordInput_ = new QLineEdit(this);
    passwordInput_->setEchoMode(QLineEdit::Password);
    labelInput_ = new QLineEdit(this);

    formLayout->addWidget(new QLabel("Stream ID", this), 0, 0);
    formLayout->addWidget(streamIdInput_, 0, 1);
    formLayout->addWidget(new QLabel("Room", this), 0, 2);
    formLayout->addWidget(roomInput_, 0, 3);
    formLayout->addWidget(new QLabel("Password", this), 1, 0);
    formLayout->addWidget(passwordInput_, 1, 1);
    formLayout->addWidget(new QLabel("Label", this), 1, 2);
    formLayout->addWidget(labelInput_, 1, 3);
    outerLayout->addLayout(formLayout);

    meterBar_ = new QProgressBar(this);
    meterBar_->setRange(0, 100);
    meterBar_->setValue(0);
    meterBar_->setFormat("Audio idle");
    outerLayout->addWidget(meterBar_);

    auto *linkLayout = new QHBoxLayout();
    viewUrlInput_ = new QLineEdit(this);
    viewUrlInput_->setReadOnly(true);
    viewUrlInput_->setPlaceholderText("View link will appear after publish");
    copyLinkButton_ = new QPushButton("Copy Link", this);
    openLinkButton_ = new QPushButton("Open", this);
    copyLinkButton_->setEnabled(false);
    openLinkButton_->setEnabled(false);
    linkLayout->addWidget(viewUrlInput_, 1);
    linkLayout->addWidget(copyLinkButton_);
    linkLayout->addWidget(openLinkButton_);
    outerLayout->addLayout(linkLayout);

    auto *actionLayout = new QHBoxLayout();
    startButton_ = new QPushButton("Start", this);
    stopButton_ = new QPushButton("Stop", this);
    removeButton_ = new QPushButton("Remove", this);
    stopButton_->setEnabled(false);
    actionLayout->addWidget(startButton_);
    actionLayout->addWidget(stopButton_);
    actionLayout->addStretch();
    actionLayout->addWidget(removeButton_);
    outerLayout->addLayout(actionLayout);

    connect(startButton_, &QPushButton::clicked, this, &RouteWidget::startRequested);
    connect(stopButton_, &QPushButton::clicked, this, &RouteWidget::stopRequested);
    connect(removeButton_, &QPushButton::clicked, this, &RouteWidget::removeRequested);
    connect(copyLinkButton_, &QPushButton::clicked, this, &RouteWidget::copyLink);
    connect(openLinkButton_, &QPushButton::clicked, this, &RouteWidget::openLink);
    connect(streamIdInput_, &QLineEdit::textChanged, this, &RouteWidget::configurationChanged);
    connect(roomInput_, &QLineEdit::textChanged, this, &RouteWidget::configurationChanged);
    connect(passwordInput_, &QLineEdit::textChanged, this, &RouteWidget::configurationChanged);
    connect(labelInput_, &QLineEdit::textChanged, this, &RouteWidget::configurationChanged);
}

QString RouteWidget::streamId() const {
    return streamIdInput_->text();
}

QString RouteWidget::room() const {
    return roomInput_->text();
}

QString RouteWidget::password() const {
    return passwordInput_->text();
}

QString RouteWidget::label() const {
    return labelInput_->text();
}

void RouteWidget::setStreamId(const QString &value) {
    streamIdInput_->setText(value);
}

void RouteWidget::setRoom(const QString &value) {
    roomInput_->setText(value);
}

void RouteWidget::setPassword(const QString &value) {
    passwordInput_->setText(value);
}

void RouteWidget::setLabelText(const QString &value) {
    labelInput_->setText(value);
}

void RouteWidget::setSourceLabel(const QString &value) {
    sourceLabel_->setText(value);
}

void RouteWidget::setStatusText(const QString &text, bool error) {
    statusLabel_->setText(text);
    statusLabel_->setProperty("error", error);
    style()->unpolish(statusLabel_);
    style()->polish(statusLabel_);
}

void RouteWidget::setStreamIdConflict(bool conflict, const QString &tooltip) {
    streamIdInput_->setProperty("error", conflict);
    streamIdInput_->setToolTip(conflict ? tooltip : QString());
    style()->unpolish(streamIdInput_);
    style()->polish(streamIdInput_);
}

void RouteWidget::setViewUrl(const QString &url) {
    viewUrlInput_->setText(url);
    const bool hasUrl = !url.trimmed().isEmpty();
    copyLinkButton_->setEnabled(hasUrl);
    openLinkButton_->setEnabled(hasUrl);
}

void RouteWidget::setMeter(float rms, float peak) {
    const int peakPercent = qBound(0, static_cast<int>(peak * 100.0f), 100);
    const int rmsPercent = qBound(0, static_cast<int>(rms * 100.0f), 100);
    meterBar_->setValue(peakPercent);
    meterBar_->setFormat(QString("RMS %1% / Peak %2%").arg(rmsPercent).arg(peakPercent));
}

void RouteWidget::setLive(bool live) {
    streamIdInput_->setEnabled(!live);
    roomInput_->setEnabled(!live);
    passwordInput_->setEnabled(!live);
    labelInput_->setEnabled(!live);
    startButton_->setEnabled(!live);
    stopButton_->setEnabled(live);
}

void RouteWidget::copyLink() {
    if (!viewUrlInput_->text().trimmed().isEmpty()) {
        QGuiApplication::clipboard()->setText(viewUrlInput_->text().trimmed());
    }
}

void RouteWidget::openLink() {
    const QUrl url(viewUrlInput_->text().trimmed());
    if (url.isValid()) {
        QDesktopServices::openUrl(url);
    }
}

}  // namespace router::ui
