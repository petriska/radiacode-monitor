#pragma once

#include <QString>

// Gzip helpers for closed spectrogram day files (.rcsg → .rcsg.gz).
// Uses zlib from Qt (QtZlib). Streaming; suitable for multi-hour files.

namespace SpectrogramCompress {

/// Compress srcPath to dstPath (gzip). Overwrites dst. Returns true on success.
bool gzipFile(const QString &srcPath, const QString &dstPath, QString *errorMessage = nullptr);

/// Decompress gzip file into outPath (raw bytes). Overwrites outPath.
bool gunzipFile(const QString &gzPath, const QString &outPath, QString *errorMessage = nullptr);

/// Compress path.rcsg → path.rcsg.gz and remove the original on success.
bool compressRcsgInPlace(const QString &rcsgPath, QString *errorMessage = nullptr);

} // namespace SpectrogramCompress
