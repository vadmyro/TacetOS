// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#include "scsisheet.h"

#include "helperconstants.h"

#if defined(Q_OS_LINUX)
#include <cerrno>
#include <cstring>
#include <scsi/sg.h>
#include <sys/ioctl.h>
#endif

namespace tacetos::helper {

bool pageData(const QByteArray &answer, quint8 page, QByteArray &data)
{
    data.clear();
    if (answer.size() < 4 || quint8(answer.at(1)) != page)
        return false;
    const int length = (quint8(answer.at(2)) << 8) | quint8(answer.at(3));
    data = answer.mid(4, length);
    return true;
}

SheetResult readDeviceSheet(const InquiryFn &inquiry)
{
    SheetResult out;
    auto fail = [&](const QLatin1String &code, const QString &detail) {
        out.ok = false;
        out.code = QString(code);
        out.detail = detail;
        out.text.clear();
        return out;
    };

    QByteArray answer;
    QString error;
    QByteArray list;
    if (!inquiry(kSheetListPage, answer, &error) || !pageData(answer, kSheetListPage, list))
        return fail(err::sheetUnsupported,
                    error.isEmpty() ? QStringLiteral("page 0xC0 is not answered") : error);

    QByteArray text;
    for (const char code : list) {
        QByteArray part;
        if (!inquiry(quint8(code), answer, &error) || !pageData(answer, quint8(code), part))
            return fail(err::sheetUnreadable, QStringLiteral("page 0x%1 cannot be read: %2")
                                                  .arg(quint8(code), 2, 16, QLatin1Char('0'))
                                                  .arg(error));
        text.append(part);
        if (text.size() > kSheetMaxBytes)
            return fail(err::sheetTooBig,
                        QStringLiteral("the sheet is longer than %1 bytes").arg(kSheetMaxBytes));
    }
    // Pages may be padded with zero bytes.
    while (!text.isEmpty() && text.endsWith('\0'))
        text.chop(1);
    if (!text.trimmed().startsWith("<?xml"))
        return fail(err::sheetUnreadable,
                    QStringLiteral("the sheet does not start like an XML document"));
    out.ok = true;
    out.text = QString::fromUtf8(text);
    return out;
}

#if defined(Q_OS_LINUX)
InquiryFn sgInquiry(int fd)
{
    return [fd](quint8 page, QByteArray &answer, QString *error) {
        unsigned char cdb[6] = {0x12, 0x01, page, 0x00, 0xFF, 0x00};
        unsigned char sense[32] = {};
        answer = QByteArray(255, '\0');
        sg_io_hdr_t hdr;
        std::memset(&hdr, 0, sizeof hdr);
        hdr.interface_id = 'S';
        hdr.dxfer_direction = SG_DXFER_FROM_DEV;
        hdr.cmd_len = sizeof cdb;
        hdr.mx_sb_len = sizeof sense;
        hdr.dxfer_len = unsigned(answer.size());
        hdr.dxferp = answer.data();
        hdr.cmdp = cdb;
        hdr.sbp = sense;
        hdr.timeout = 5000;
        if (ioctl(fd, SG_IO, &hdr) < 0) {
            if (error)
                *error = QString::fromLocal8Bit(std::strerror(errno));
            return false;
        }
        if (hdr.status != 0 || hdr.host_status != 0 || hdr.driver_status != 0) {
            if (error)
                *error = QStringLiteral("SCSI status %1").arg(hdr.status);
            return false;
        }
        answer.truncate(answer.size() - hdr.resid);
        return true;
    };
}
#endif

} // namespace tacetos::helper
