// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "fakesystemlayer.h"

namespace tacetos::installer {

namespace {

// Hands out the link of a device that the test owns.
class LinkProxy : public UsbDfuLink
{
public:
    explicit LinkProxy(UsbDfuLink *target) : m_target(target) {}
    UsbDeviceId deviceId() const override { return m_target->deviceId(); }
    bool open(QString *error) override { return m_target->open(error); }
    UsbAccessError openError() const override { return m_target->openError(); }
    UsbControlResult control(const UsbControlRequest &request, const QByteArray &out,
                             int timeoutMs) override
    {
        return m_target->control(request, out, timeoutMs);
    }
    bool reset(QString *error) override { return m_target->reset(error); }
    void close() override { m_target->close(); }

private:
    UsbDfuLink *m_target;
};

} // namespace

QList<UsbDeviceInfo> FakeSystemLayer::listUsbDevices()
{
    QList<UsbDeviceInfo> list;
    for (const FakeBusStep &step : std::as_const(bus)) {
        if (step.fromMs <= now)
            list = step.devices;
    }
    return list;
}

std::unique_ptr<UsbDfuLink> FakeSystemLayer::makeLink(const UsbDeviceInfo &)
{
    linkTimes.append(now);
    return std::make_unique<LinkProxy>(device);
}

bool FakeSystemLayer::unmountIpod(QString *error)
{
    ++unmountCalls;
    if (unmountCalls <= unmountFailures) {
        if (error)
            *error = QStringLiteral("target is busy");
        return false;
    }
    events << QStringLiteral("unmounted");
    return true;
}

bool FakeSystemLayer::ipodListed()
{
    if (listedPolls > 0) {
        --listedPolls;
        return true;
    }
    return false;
}

bool FakeSystemLayer::pauseProcess(const SystemProcess &process, QString *error)
{
    if (pauseFails.contains(process.id)) {
        if (error)
            *error = QStringLiteral("not permitted");
        return false;
    }
    events << QStringLiteral("pause %1").arg(process.id);
    return true;
}

bool FakeSystemLayer::resumeProcess(const SystemProcess &process, QString *error)
{
    if (resumeFails.contains(process.id)) {
        if (error)
            *error = QStringLiteral("not permitted");
        return false;
    }
    events << QStringLiteral("resume %1").arg(process.id);
    return true;
}

AroundOptions FakeSystemLayer::options()
{
    AroundOptions o;
    o.sleep = [this](int ms) {
        sleeps.append(ms);
        now += ms;
    };
    o.cancelled = [this]() { return cancelAtMs >= 0 && now >= cancelAtMs; };
    o.notify = [this](const QString &text) { notices << text; };
    return o;
}

} // namespace tacetos::installer
