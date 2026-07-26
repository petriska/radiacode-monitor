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

// Strip .csv / .tka / .n42 (case-insensitive) from path/basename.
QString stripKnownExtension(const QString &path);
// Ensure path ends with the correct extension for format (replace wrong one).
QString withExtension(const QString &path, Format format);

} // namespace SpectrumExport
