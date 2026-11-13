// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vadmyro

#pragma once

#include <QByteArray>
#include <QString>
#include <functional>

// The device sheet (docs/specs/helper.md section 6.3): the property list the iPod
// gives through SCSI INQUIRY requests for vendor pages. INQUIRY is the only SCSI
// command the helper ever sends.

namespace tacetos::helper {

inline constexpr int kSheetMaxBytes = 65536;
inline constexpr quint8 kSheetListPage = 0xC0;

// One INQUIRY with the EVPD bit set for `page`. Fills `answer` with everything the
// device sent, header included. False, with the reason, when the request failed.
using InquiryFn = std::function<bool(quint8 page, QByteArray &answer, QString *error)>;

struct SheetResult
{
    bool ok = false;
    // `sheet-unsupported`, `sheet-too-big` or `sheet-unreadable` when not ok.
    QString code;
    QString detail;
    QString text;
};

// The data of a page: what follows its 4-byte header, as long as the length in
// the header says (never more than the answer holds). The page code of the answer
// must be the one asked for.
bool pageData(const QByteArray &answer, quint8 page, QByteArray &data);

// Section 6.3 steps 2 to 4.
SheetResult readDeviceSheet(const InquiryFn &inquiry);

#if defined(Q_OS_LINUX)
// The request of Linux for generic SCSI (`SG_IO`), on an open file of the disk
// (section 11.4). Never sends anything but INQUIRY.
InquiryFn sgInquiry(int fd);
#endif

} // namespace tacetos::helper
