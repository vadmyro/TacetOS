// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "mountwatcher.h"

namespace tacetos::device {

MountWatcher::MountWatcher(QString path, QObject *parent)
    : EventSource(parent), path_(std::move(path)), file_(path_)
{
}

MountWatcher::~MountWatcher()
{
    delete notifier_;
}

bool MountWatcher::open()
{
    if (file_.isOpen())
        return true;
    if (!file_.open(QIODevice::ReadOnly))
        return false;
    const int fd = file_.handle();
    if (fd < 0) {
        file_.close();
        return false;
    }

    notifier_ = new QSocketNotifier(fd, QSocketNotifier::Exception, this);
    connect(notifier_, &QSocketNotifier::activated, this, [this] { reportChange(); });
    return true;
}

MountWatcher *MountWatcher::attach(LiveDriver *driver, const QString &path)
{
    auto *watcher = new MountWatcher(path, driver);
    if (!watcher->open()) {
        delete watcher;
        return nullptr;
    }
    driver->addEventSource(watcher);
    return watcher;
}

} // namespace tacetos::device
