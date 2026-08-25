#include "spectrogramcompress.h"

#include <QFile>

#include <cstring>

// Distro Qt (Ubuntu/Debian) is built against system zlib — there is no QtZlib.
// Official Qt kits ship headers as <QtZlib/zlib.h>.
#if defined(__has_include)
#  if __has_include(<zlib.h>)
#    include <zlib.h>
#  elif __has_include(<QtZlib/zlib.h>)
#    include <QtZlib/zlib.h>
#  else
#    error "zlib headers not found. Ubuntu/Debian: sudo apt install zlib1g-dev"
#  endif
#else
#  include <zlib.h>
#endif

namespace SpectrogramCompress {
namespace {

void setError(QString *errorMessage, const QString &msg)
{
    if (errorMessage) {
        *errorMessage = msg;
    }
}

constexpr int kChunk = 1 << 16;

} // namespace

bool gzipFile(const QString &srcPath, const QString &dstPath, QString *errorMessage)
{
    QFile in(srcPath);
    if (!in.open(QIODevice::ReadOnly)) {
        setError(errorMessage, in.errorString());
        return false;
    }
    QFile out(dstPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(errorMessage, out.errorString());
        return false;
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    // windowBits 15+16 = gzip wrapper
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY)
        != Z_OK) {
        setError(errorMessage, QStringLiteral("deflateInit2 failed"));
        return false;
    }

    QByteArray inBuf(kChunk, Qt::Uninitialized);
    QByteArray outBuf(kChunk, Qt::Uninitialized);
    int ret = Z_OK;
    do {
        const qint64 nread = in.read(inBuf.data(), inBuf.size());
        if (nread < 0) {
            deflateEnd(&strm);
            setError(errorMessage, in.errorString());
            return false;
        }
        strm.avail_in = uInt(nread);
        strm.next_in = reinterpret_cast<Bytef *>(inBuf.data());
        const int flush = in.atEnd() ? Z_FINISH : Z_NO_FLUSH;
        do {
            strm.avail_out = uInt(outBuf.size());
            strm.next_out = reinterpret_cast<Bytef *>(outBuf.data());
            ret = deflate(&strm, flush);
            if (ret == Z_STREAM_ERROR) {
                deflateEnd(&strm);
                setError(errorMessage, QStringLiteral("deflate stream error"));
                return false;
            }
            const int have = outBuf.size() - int(strm.avail_out);
            if (have > 0 && out.write(outBuf.constData(), have) != have) {
                deflateEnd(&strm);
                setError(errorMessage, out.errorString());
                return false;
            }
        } while (strm.avail_out == 0);
    } while (ret != Z_STREAM_END);

    deflateEnd(&strm);
    return true;
}

bool gunzipFile(const QString &gzPath, const QString &outPath, QString *errorMessage)
{
    QFile in(gzPath);
    if (!in.open(QIODevice::ReadOnly)) {
        setError(errorMessage, in.errorString());
        return false;
    }
    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(errorMessage, out.errorString());
        return false;
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, 15 + 16) != Z_OK) {
        setError(errorMessage, QStringLiteral("inflateInit2 failed"));
        return false;
    }

    QByteArray inBuf(kChunk, Qt::Uninitialized);
    QByteArray outBuf(kChunk, Qt::Uninitialized);
    int ret = Z_OK;
    do {
        const qint64 nread = in.read(inBuf.data(), inBuf.size());
        if (nread < 0) {
            inflateEnd(&strm);
            setError(errorMessage, in.errorString());
            return false;
        }
        if (nread == 0 && ret == Z_OK) {
            break;
        }
        strm.avail_in = uInt(nread);
        strm.next_in = reinterpret_cast<Bytef *>(inBuf.data());
        do {
            strm.avail_out = uInt(outBuf.size());
            strm.next_out = reinterpret_cast<Bytef *>(outBuf.data());
            ret = inflate(&strm, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                inflateEnd(&strm);
                setError(errorMessage, QStringLiteral("inflate error %1").arg(ret));
                return false;
            }
            const int have = outBuf.size() - int(strm.avail_out);
            if (have > 0 && out.write(outBuf.constData(), have) != have) {
                inflateEnd(&strm);
                setError(errorMessage, out.errorString());
                return false;
            }
        } while (strm.avail_out == 0);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    if (ret != Z_STREAM_END) {
        setError(errorMessage, QStringLiteral("Truncated or corrupt gzip."));
        return false;
    }
    return true;
}

bool compressRcsgInPlace(const QString &rcsgPath, QString *errorMessage)
{
    if (!rcsgPath.endsWith(QStringLiteral(".rcsg"), Qt::CaseInsensitive)) {
        setError(errorMessage, QStringLiteral("Not a .rcsg path."));
        return false;
    }
    const QString gzPath = rcsgPath + QStringLiteral(".gz");
    if (!gzipFile(rcsgPath, gzPath, errorMessage)) {
        QFile::remove(gzPath);
        return false;
    }
    if (!QFile::remove(rcsgPath)) {
        setError(errorMessage, QStringLiteral("Compressed OK but could not remove original."));
        // Keep .gz; original still there — not fatal for data safety
        return false;
    }
    return true;
}

} // namespace SpectrogramCompress
