#pragma once

#include "protocol/types.h"

#include <QString>

namespace SpectrumExport {

enum class Format {
    Csv,
    Tka,
    N42,
};

// Write spectrum to path in the selected format.
// Returns empty QString on success, or an error message.
QString writeSpectrumFile(
    const QString &path,
    Format format,
    const QtRadiacode::RcSpectrum &spectrum,
    const QString &deviceSerial,
    const QString &firmwareVersion);

QString formatFilterString();
Format formatFromFilter(const QString &selectedFilter);
QString defaultExtension(Format format);

} // namespace SpectrumExport
