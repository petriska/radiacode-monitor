#pragma once

#include "protocol/types.h"

#include <QString>

namespace SpectrumExport {

enum class Format {
    Csv,
    Tka,
    N42,
    Npes, // OpenGammaProject NPES-JSON (NPESv2)
};

// Write spectrum to path in the selected format.
// Returns empty QString on success, or an error message.
QString writeSpectrumFile(
    const QString &path,
    Format format,
    const QtRadiacode::RcSpectrum &spectrum,
    const QString &deviceSerial,
    const QString &firmwareVersion);

// Read spectrum from path. Format is inferred from extension when possible;
// if ambiguous, content heuristics are used (CSV headers / NPES JSON / TKA / N42).
// On success: *out is filled and return value is empty.
// On failure: return non-empty error message; *out is unchanged.
QString readSpectrumFile(const QString &path, QtRadiacode::RcSpectrum *out);

// Open-dialog filter string (same family as export; "All supported" first).
QString openFormatFilterString();
// Guess format from path extension; returns Csv as weak default.
Format formatFromPath(const QString &path);

QString formatFilterString();
Format formatFromFilter(const QString &selectedFilter);
// Exact name-filter entry for QFileDialog::selectNameFilter (no "All files").
QString nameFilterForFormat(Format format);
// Stable keys for QSettings: "csv" | "tka" | "n42" | "npes"
QString formatSettingsKey(Format format);
Format formatFromSettingsKey(const QString &key);
QString defaultExtension(Format format);

// Strip known export extensions (.csv / .tka / .n42 / .json) from path/basename.
QString stripKnownExtension(const QString &path);
// Ensure path ends with the correct extension for format (replace wrong one).
QString withExtension(const QString &path, Format format);

} // namespace SpectrumExport
