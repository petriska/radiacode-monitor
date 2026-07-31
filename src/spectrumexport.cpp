#include "spectrumexport.h"

#include "protocol/types.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QTextStream>
#include <QUuid>
#include <QXmlStreamReader>

namespace SpectrumExport {

namespace {

QString xmlEscape(const QString &s)
{
    QString o = s;
    o.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    o.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    o.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    o.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    return o;
}

QString modelFromSerial(const QString &serial)
{
    // RC-110-004894 → RadiaCode-110
    const QStringList parts = serial.split(QLatin1Char('-'));
    if (parts.size() >= 2 && parts.at(0) == QLatin1String("RC")) {
        return QStringLiteral("RadiaCode-%1").arg(parts.at(1));
    }
    return QStringLiteral("RadiaCode");
}

bool writeCsv(
    QFile &f,
    const QtRadiacode::RcSpectrum &sp,
    const QString &serial,
    const QString &fw)
{
    QTextStream out(&f);
    out.setRealNumberNotation(QTextStream::SmartNotation);
    out.setRealNumberPrecision(8);

    out << "# QtRadiacode spectrum export\n";
    out << "# format=csv\n";
    out << "# saved_utc=" << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n";
    out << "# device_serial=" << serial << "\n";
    out << "# firmware=" << fw << "\n";
    out << "# live_time_sec=" << sp.durationSec << "\n";
    out << "# calib_a0=" << sp.a0 << "\n";
    out << "# calib_a1=" << sp.a1 << "\n";
    out << "# calib_a2=" << sp.a2 << "\n";
    out << "# channels=" << sp.counts.size() << "\n";
    out << "# energy_keV = a0 + a1*channel + a2*channel^2\n";
    out << "channel,counts,energy_keV\n";

    for (int i = 0; i < sp.counts.size(); ++i) {
        const double e =
            QtRadiacode::spectrumChannelToEnergy(i, sp.a0, sp.a1, sp.a2);
        out << i << ',' << sp.counts.at(i) << ',' << e << '\n';
    }
    return true;
}

// TKA (common gamma-spectroscopy interchange, Genie/InterSpec/etc.):
//   line 1: live time (s)
//   line 2: real time (s)
//   then one count per channel (channel 0 .. N-1)
// Radiacode exposes a single accumulation duration — use it for both times.
bool writeTka(QFile &f, const QtRadiacode::RcSpectrum &sp)
{
    QTextStream out(&f);
    out << sp.durationSec << '\n';
    out << sp.durationSec << '\n';
    for (quint32 c : sp.counts) {
        out << c << '\n';
    }
    return true;
}

// Minimal ANSI/IEEE N42.42-2011 RadInstrumentData (foreground only),
// aligned with radiacode-tools RcN42 structure.
bool writeN42(
    QFile &f,
    const QtRadiacode::RcSpectrum &sp,
    const QString &serial,
    const QString &fw)
{
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString model = modelFromSerial(serial);
    const QString start = QDateTime::currentDateTimeUtc()
                              .addSecs(-static_cast<qint64>(sp.durationSec))
                              .toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    const QString duration = QStringLiteral("PT%1S").arg(sp.durationSec);
    const QString detId = QStringLiteral("radiacode-scinitillator-sipm");
    const QString title = xmlEscape(
        QStringLiteral("%1 spectrum %2 s").arg(serial).arg(sp.durationSec));

    QString channelData;
    channelData.reserve(sp.counts.size() * 6);
    for (int i = 0; i < sp.counts.size(); ++i) {
        if (i) {
            channelData += QLatin1Char(' ');
        }
        channelData += QString::number(sp.counts.at(i));
    }

    const QString cal = QStringLiteral("%1 %2 %3")
                            .arg(sp.a0, 0, 'f', 7)
                            .arg(sp.a1, 0, 'f', 7)
                            .arg(sp.a2, 0, 'f', 7);

    // Detector size hints from radiacode-tools (approximate).
    QString lenMm = QStringLiteral("10");
    QString volCc = QStringLiteral("1");
    QString kind = QStringLiteral("CsI");
    QString desc = QStringLiteral("CsI:Tl scintillator, coupled to SiPM");
    if (model.contains(QLatin1String("110"))) {
        lenMm = QStringLiteral("14.5");
        volCc = QStringLiteral("3");
    } else if (model.contains(QLatin1String("103"))) {
        volCc = QStringLiteral("3");
    }
    if (model.endsWith(QLatin1Char('G'))) {
        kind = QStringLiteral("GaGG");
        desc = QStringLiteral("GaGG:Ce scintillator, coupled to SiPM");
    }

    QTextStream out(&f);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<RadInstrumentData\n";
    out << "  xmlns=\"http://physics.nist.gov/N42/2011/N42\"\n";
    out << "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n";
    out << "  xsi:schemaLocation=\"http://physics.nist.gov/N42/2011/N42 "
           "http://physics.nist.gov/N42/2011/n42.xsd\"\n";
    out << "  n42DocUUID=\"" << uuid << "\">\n";
    out << "  <RadInstrumentDataCreatorName>radiacode-monitor / QtRadiacode</"
           "RadInstrumentDataCreatorName>\n";
    out << "  <RadInstrumentInformation id=\"radiacode-instrument-info\">\n";
    out << "    <RadInstrumentManufacturerName>Radiacode</RadInstrumentManufacturerName>\n";
    out << "    <RadInstrumentIdentifier>" << xmlEscape(serial)
        << "</RadInstrumentIdentifier>\n";
    out << "    <RadInstrumentModelName>" << xmlEscape(model)
        << "</RadInstrumentModelName>\n";
    out << "    <RadInstrumentClassCode>Spectroscopic Personal Radiation "
           "Detector</RadInstrumentClassCode>\n";
    out << "    <RadInstrumentVersion>\n";
    out << "      <RadInstrumentComponentName>Firmware</RadInstrumentComponentName>\n";
    out << "      <RadInstrumentComponentVersion>" << xmlEscape(fw)
        << "</RadInstrumentComponentVersion>\n";
    out << "    </RadInstrumentVersion>\n";
    out << "    <RadInstrumentVersion>\n";
    out << "      <RadInstrumentComponentName>radiacode-monitor</"
           "RadInstrumentComponentName>\n";
    out << "      <RadInstrumentComponentVersion>0.1.0</"
           "RadInstrumentComponentVersion>\n";
    out << "    </RadInstrumentVersion>\n";
    out << "  </RadInstrumentInformation>\n";
    out << "  <RadDetectorInformation id=\"" << detId << "\">\n";
    out << "    <RadDetectorCategoryCode>Gamma</RadDetectorCategoryCode>\n";
    out << "    <RadDetectorKindCode>" << kind << "</RadDetectorKindCode>\n";
    out << "    <RadDetectorDescription>" << desc << "</RadDetectorDescription>\n";
    out << "    <RadDetectorLengthValue units=\"mm\">" << lenMm
        << "</RadDetectorLengthValue>\n";
    out << "    <RadDetectorWidthValue units=\"mm\">" << lenMm
        << "</RadDetectorWidthValue>\n";
    out << "    <RadDetectorDepthValue units=\"mm\">" << lenMm
        << "</RadDetectorDepthValue>\n";
    out << "    <RadDetectorVolumeValue units=\"cc\">" << volCc
        << "</RadDetectorVolumeValue>\n";
    out << "  </RadDetectorInformation>\n";
    out << "  <EnergyCalibration id=\"ec-fg\">\n";
    out << "    <CoefficientValues>" << cal << "</CoefficientValues>\n";
    out << "  </EnergyCalibration>\n";
    out << "  <RadMeasurement id=\"radmeas-fg\">\n";
    out << "    <Remark>Title: " << title << "</Remark>\n";
    out << "    <MeasurementClassCode>Foreground</MeasurementClassCode>\n";
    out << "    <StartDateTime>" << start << "</StartDateTime>\n";
    out << "    <RealTimeDuration>" << duration << "</RealTimeDuration>\n";
    out << "    <Spectrum id=\"spectrum-fg\" radDetectorInformationReference=\""
        << detId << "\" energyCalibrationReference=\"ec-fg\">\n";
    out << "      <LiveTimeDuration>" << duration << "</LiveTimeDuration>\n";
    out << "      <ChannelData compressionCode=\"None\">" << channelData
        << "</ChannelData>\n";
    out << "    </Spectrum>\n";
    out << "  </RadMeasurement>\n";
    out << "</RadInstrumentData>\n";
    return true;
}

// OpenGammaProject NPES-JSON NPESv2:
// https://github.com/OpenGammaProject/NPES-JSON
bool writeNpes(
    QFile &f,
    const QtRadiacode::RcSpectrum &sp,
    const QString &serial,
    const QString &fw)
{
    quint64 totalCounts = 0;
    QJsonArray spectrumArr;
    for (quint32 c : sp.counts) {
        spectrumArr.append(static_cast<qint64>(c));
        totalCounts += c;
    }

    QJsonObject energyCalibration;
    energyCalibration.insert(QStringLiteral("polynomialOrder"), 2);
    QJsonArray coeffs;
    coeffs.append(sp.a0);
    coeffs.append(sp.a1);
    coeffs.append(sp.a2);
    energyCalibration.insert(QStringLiteral("coefficients"), coeffs);

    QJsonObject energySpectrum;
    energySpectrum.insert(QStringLiteral("numberOfChannels"), sp.counts.size());
    energySpectrum.insert(QStringLiteral("spectrum"), spectrumArr);
    energySpectrum.insert(QStringLiteral("energyCalibration"), energyCalibration);
    // Schema requires measurementTime >= 1 when present.
    const int measTime = sp.durationSec > 0 ? static_cast<int>(sp.durationSec) : 1;
    energySpectrum.insert(QStringLiteral("measurementTime"), measTime);
    if (totalCounts > 0) {
        energySpectrum.insert(QStringLiteral("validPulseCount"),
                              static_cast<qint64>(totalCounts));
    }

    const QDateTime end = QDateTime::currentDateTimeUtc();
    const QDateTime start = end.addSecs(-static_cast<qint64>(sp.durationSec));

    QJsonObject resultData;
    resultData.insert(QStringLiteral("startTime"),
                      start.toString(Qt::ISODate));
    resultData.insert(QStringLiteral("endTime"), end.toString(Qt::ISODate));
    resultData.insert(QStringLiteral("energySpectrum"), energySpectrum);

    QJsonObject deviceData;
    deviceData.insert(QStringLiteral("softwareName"),
                      QStringLiteral("radiacode-monitor 0.1.0 (QtRadiacode)"));
    if (!serial.isEmpty() && serial != QLatin1String("unknown")) {
        deviceData.insert(QStringLiteral("deviceName"), serial);
    }
    // Extra metadata allowed (additionalProperties: true on deviceData).
    if (!fw.isEmpty() && fw != QLatin1String("unknown")) {
        deviceData.insert(QStringLiteral("firmwareVersion"), fw);
    }

    QJsonObject package;
    package.insert(QStringLiteral("deviceData"), deviceData);
    package.insert(QStringLiteral("resultData"), resultData);

    QJsonArray dataArr;
    dataArr.append(package);

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), QStringLiteral("NPESv2"));
    root.insert(QStringLiteral("data"), dataArr);

    const QByteArray json =
        QJsonDocument(root).toJson(QJsonDocument::Indented);
    return f.write(json) == json.size();
}

} // namespace

QString formatFilterString()
{
    return QStringLiteral(
        "CSV (*.csv);;"
        "TKA spectrum (*.tka);;"
        "ANSI N42.42 (*.n42);;"
        "NPES-JSON (*.json);;"
        "All files (*)");
}

Format formatFromFilter(const QString &selectedFilter)
{
    if (selectedFilter.contains(QLatin1String("TKA"), Qt::CaseInsensitive)
        || selectedFilter.contains(QLatin1String("*.tka"), Qt::CaseInsensitive)) {
        return Format::Tka;
    }
    if (selectedFilter.contains(QLatin1String("N42"), Qt::CaseInsensitive)
        || selectedFilter.contains(QLatin1String("*.n42"), Qt::CaseInsensitive)) {
        return Format::N42;
    }
    if (selectedFilter.contains(QLatin1String("NPES"), Qt::CaseInsensitive)
        || selectedFilter.contains(QLatin1String("*.json"), Qt::CaseInsensitive)) {
        return Format::Npes;
    }
    return Format::Csv;
}

QString nameFilterForFormat(Format format)
{
    switch (format) {
    case Format::Tka:
        return QStringLiteral("TKA spectrum (*.tka)");
    case Format::N42:
        return QStringLiteral("ANSI N42.42 (*.n42)");
    case Format::Npes:
        return QStringLiteral("NPES-JSON (*.json)");
    case Format::Csv:
    default:
        return QStringLiteral("CSV (*.csv)");
    }
}

QString formatSettingsKey(Format format)
{
    switch (format) {
    case Format::Tka:
        return QStringLiteral("tka");
    case Format::N42:
        return QStringLiteral("n42");
    case Format::Npes:
        return QStringLiteral("npes");
    case Format::Csv:
    default:
        return QStringLiteral("csv");
    }
}

Format formatFromSettingsKey(const QString &key)
{
    const QString k = key.trimmed().toLower();
    if (k == QLatin1String("tka")) {
        return Format::Tka;
    }
    if (k == QLatin1String("n42") || k == QLatin1String("n42.42")) {
        return Format::N42;
    }
    if (k == QLatin1String("npes") || k == QLatin1String("json")
        || k == QLatin1String("npes-json")) {
        return Format::Npes;
    }
    return Format::Csv;
}

QString defaultExtension(Format format)
{
    switch (format) {
    case Format::Tka:
        return QStringLiteral("tka");
    case Format::N42:
        return QStringLiteral("n42");
    case Format::Npes:
        return QStringLiteral("json");
    case Format::Csv:
    default:
        return QStringLiteral("csv");
    }
}

QString stripKnownExtension(const QString &path)
{
    QString p = path;
    static const QStringList exts = {
        QStringLiteral(".csv"),
        QStringLiteral(".tka"),
        QStringLiteral(".n42"),
        QStringLiteral(".json"),
    };
    for (const QString &e : exts) {
        if (p.endsWith(e, Qt::CaseInsensitive)) {
            p.chop(e.size());
            break;
        }
    }
    return p;
}

QString withExtension(const QString &path, Format format)
{
    return stripKnownExtension(path) + QLatin1Char('.') + defaultExtension(format);
}

QString writeSpectrumFile(
    const QString &path,
    Format format,
    const QtRadiacode::RcSpectrum &spectrum,
    const QString &deviceSerial,
    const QString &firmwareVersion)
{
    if (spectrum.counts.isEmpty()) {
        return QStringLiteral("Spectrum has no channels");
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QStringLiteral("Cannot write file: %1").arg(path);
    }

    const QString serial =
        deviceSerial.isEmpty() ? QStringLiteral("unknown") : deviceSerial;
    const QString fw =
        firmwareVersion.isEmpty() ? QStringLiteral("unknown") : firmwareVersion;

    bool ok = false;
    switch (format) {
    case Format::Csv:
        ok = writeCsv(f, spectrum, serial, fw);
        break;
    case Format::Tka:
        ok = writeTka(f, spectrum);
        break;
    case Format::N42:
        ok = writeN42(f, spectrum, serial, fw);
        break;
    case Format::Npes:
        ok = writeNpes(f, spectrum, serial, fw);
        break;
    }
    f.close();
    if (!ok) {
        return QStringLiteral("Failed to write spectrum data");
    }
    return {};
}

namespace {

quint32 parseDurationPtSeconds(const QString &pt)
{
    // PT123S or PT1H2M3S (we mainly write PT%nS).
    static const QRegularExpression re(
        QStringLiteral(R"(PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+(?:\.\d+)?)S)?)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(pt.trimmed());
    if (!m.hasMatch()) {
        bool ok = false;
        const quint32 v = pt.trimmed().toUInt(&ok);
        return ok ? v : 0u;
    }
    const quint32 h = m.captured(1).isEmpty() ? 0u : m.captured(1).toUInt();
    const quint32 min = m.captured(2).isEmpty() ? 0u : m.captured(2).toUInt();
    const double sec = m.captured(3).isEmpty() ? 0.0 : m.captured(3).toDouble();
    return h * 3600u + min * 60u + static_cast<quint32>(sec + 0.5);
}

bool parseCsv(const QString &text, QtRadiacode::RcSpectrum *out, QString *err)
{
    QtRadiacode::RcSpectrum sp;
    QVector<quint32> counts;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                         Qt::SkipEmptyParts);
    bool inData = false;
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char('#'))) {
            // # live_time_sec=123
            // # calib_a0=...
            const QString body = line.mid(1).trimmed();
            const int eq = body.indexOf(QLatin1Char('='));
            if (eq <= 0) {
                continue;
            }
            const QString key = body.left(eq).trimmed().toLower();
            const QString val = body.mid(eq + 1).trimmed();
            bool ok = false;
            if (key == QLatin1String("live_time_sec") || key == QLatin1String("livetime")
                || key == QLatin1String("live_time")) {
                sp.durationSec = val.toUInt(&ok);
            } else if (key == QLatin1String("calib_a0") || key == QLatin1String("a0")) {
                sp.a0 = val.toFloat(&ok);
            } else if (key == QLatin1String("calib_a1") || key == QLatin1String("a1")) {
                sp.a1 = val.toFloat(&ok);
            } else if (key == QLatin1String("calib_a2") || key == QLatin1String("a2")) {
                sp.a2 = val.toFloat(&ok);
            }
            continue;
        }
        if (line.startsWith(QLatin1String("channel"), Qt::CaseInsensitive)) {
            inData = true;
            continue;
        }
        // channel,counts[,energy]
        const QStringList parts = line.split(QLatin1Char(','));
        if (parts.size() < 2) {
            if (err) {
                *err = QStringLiteral("CSV: expected channel,counts on line: %1").arg(line);
            }
            return false;
        }
        bool okCh = false;
        bool okC = false;
        const int ch = parts.at(0).trimmed().toInt(&okCh);
        const quint32 c = parts.at(1).trimmed().toUInt(&okC);
        if (!okCh || !okC || ch < 0) {
            if (err) {
                *err = QStringLiteral("CSV: bad channel/counts: %1").arg(line);
            }
            return false;
        }
        if (ch >= counts.size()) {
            counts.resize(ch + 1);
        }
        counts[ch] = c;
        inData = true;
        Q_UNUSED(inData);
    }
    if (counts.isEmpty()) {
        if (err) {
            *err = QStringLiteral("CSV: no channel data found");
        }
        return false;
    }
    sp.counts = counts;
    *out = sp;
    return true;
}

bool parseTka(const QString &text, QtRadiacode::RcSpectrum *out, QString *err)
{
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                         Qt::SkipEmptyParts);
    if (lines.size() < 3) {
        if (err) {
            *err = QStringLiteral("TKA: need live time, real time, and channel counts");
        }
        return false;
    }
    bool okLive = false;
    bool okReal = false;
    const double live = lines.at(0).trimmed().toDouble(&okLive);
    lines.at(1).trimmed().toDouble(&okReal); // real time — ignore value
    if (!okLive || !okReal) {
        if (err) {
            *err = QStringLiteral("TKA: invalid live/real time header");
        }
        return false;
    }
    QtRadiacode::RcSpectrum sp;
    sp.durationSec = live > 0.0 ? static_cast<quint32>(live + 0.5) : 0u;
    sp.counts.reserve(lines.size() - 2);
    for (int i = 2; i < lines.size(); ++i) {
        bool ok = false;
        const quint32 c = lines.at(i).trimmed().toUInt(&ok);
        if (!ok) {
            if (err) {
                *err = QStringLiteral("TKA: bad count on line %1").arg(i + 1);
            }
            return false;
        }
        sp.counts.append(c);
    }
    if (sp.counts.isEmpty()) {
        if (err) {
            *err = QStringLiteral("TKA: no channels");
        }
        return false;
    }
    // TKA has no energy calibration — leave a0/a1/a2 at 0.
    *out = sp;
    return true;
}

bool parseN42(const QString &text, QtRadiacode::RcSpectrum *out, QString *err)
{
    QXmlStreamReader xml(text);
    QtRadiacode::RcSpectrum sp;
    QString channelData;
    QString coeffText;
    QString livePt;

    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        const QStringView name = xml.name();
        if (name == QLatin1String("LiveTimeDuration")
            || name == QLatin1String("RealTimeDuration")) {
            if (livePt.isEmpty() || name == QLatin1String("LiveTimeDuration")) {
                livePt = xml.readElementText().trimmed();
            }
        } else if (name == QLatin1String("CoefficientValues")) {
            coeffText = xml.readElementText().trimmed();
        } else if (name == QLatin1String("ChannelData")) {
            channelData = xml.readElementText().trimmed();
        }
    }
    if (xml.hasError()) {
        if (err) {
            *err = QStringLiteral("N42 XML: %1").arg(xml.errorString());
        }
        return false;
    }
    if (channelData.isEmpty()) {
        if (err) {
            *err = QStringLiteral("N42: ChannelData not found");
        }
        return false;
    }

    const QStringList toks =
        channelData.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    sp.counts.reserve(toks.size());
    for (const QString &t : toks) {
        bool ok = false;
        const quint32 c = t.toUInt(&ok);
        if (!ok) {
            if (err) {
                *err = QStringLiteral("N42: bad channel count '%1'").arg(t);
            }
            return false;
        }
        sp.counts.append(c);
    }
    if (sp.counts.isEmpty()) {
        if (err) {
            *err = QStringLiteral("N42: empty ChannelData");
        }
        return false;
    }

    if (!livePt.isEmpty()) {
        sp.durationSec = parseDurationPtSeconds(livePt);
    }

    if (!coeffText.isEmpty()) {
        const QStringList c =
            coeffText.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (c.size() >= 1) {
            sp.a0 = c.at(0).toFloat();
        }
        if (c.size() >= 2) {
            sp.a1 = c.at(1).toFloat();
        }
        if (c.size() >= 3) {
            sp.a2 = c.at(2).toFloat();
        }
    }

    *out = sp;
    return true;
}

bool parseNpes(const QByteArray &raw, QtRadiacode::RcSpectrum *out, QString *err)
{
    QJsonParseError pe {};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (err) {
            *err = QStringLiteral("NPES-JSON: parse error: %1").arg(pe.errorString());
        }
        return false;
    }
    const QJsonObject root = doc.object();
    const QJsonArray data = root.value(QStringLiteral("data")).toArray();
    if (data.isEmpty()) {
        if (err) {
            *err = QStringLiteral("NPES-JSON: data[] empty");
        }
        return false;
    }
    const QJsonObject package = data.at(0).toObject();
    const QJsonObject result = package.value(QStringLiteral("resultData")).toObject();
    QJsonObject energySpectrum = result.value(QStringLiteral("energySpectrum")).toObject();
    // Some files nest under first result only — already handled.
    if (energySpectrum.isEmpty()) {
        // Fallback: energySpectrum at package root (non-standard).
        energySpectrum = package.value(QStringLiteral("energySpectrum")).toObject();
    }
    if (energySpectrum.isEmpty()) {
        if (err) {
            *err = QStringLiteral("NPES-JSON: energySpectrum not found");
        }
        return false;
    }

    QtRadiacode::RcSpectrum sp;
    const QJsonArray spectrumArr = energySpectrum.value(QStringLiteral("spectrum")).toArray();
    if (spectrumArr.isEmpty()) {
        if (err) {
            *err = QStringLiteral("NPES-JSON: spectrum array empty");
        }
        return false;
    }
    sp.counts.reserve(spectrumArr.size());
    for (const QJsonValue &v : spectrumArr) {
        sp.counts.append(static_cast<quint32>(v.toVariant().toULongLong()));
    }

    const int meas = energySpectrum.value(QStringLiteral("measurementTime")).toInt(0);
    if (meas > 0) {
        sp.durationSec = static_cast<quint32>(meas);
    }

    const QJsonObject cal =
        energySpectrum.value(QStringLiteral("energyCalibration")).toObject();
    const QJsonArray coeffs = cal.value(QStringLiteral("coefficients")).toArray();
    if (coeffs.size() >= 1) {
        sp.a0 = static_cast<float>(coeffs.at(0).toDouble());
    }
    if (coeffs.size() >= 2) {
        sp.a1 = static_cast<float>(coeffs.at(1).toDouble());
    }
    if (coeffs.size() >= 3) {
        sp.a2 = static_cast<float>(coeffs.at(2).toDouble());
    }

    *out = sp;
    return true;
}

Format guessFormatFromContent(const QByteArray &raw)
{
    const QByteArray trimmed = raw.trimmed();
    if (trimmed.startsWith('{') || trimmed.startsWith('[')) {
        return Format::Npes;
    }
    if (trimmed.startsWith("<?xml") || trimmed.contains("<RadInstrumentData")
        || trimmed.contains("<ChannelData")) {
        return Format::N42;
    }
    const QString head = QString::fromUtf8(trimmed.left(512));
    if (head.contains(QLatin1String("channel,counts"), Qt::CaseInsensitive)
        || head.contains(QLatin1String("# format=csv"))
        || head.contains(QLatin1String("calib_a0"))) {
        return Format::Csv;
    }
    // Default numeric dump → TKA
    return Format::Tka;
}

} // namespace

QString openFormatFilterString()
{
    return QStringLiteral(
        "Spectrum files (*.csv *.tka *.n42 *.json);;"
        "CSV (*.csv);;"
        "TKA spectrum (*.tka);;"
        "ANSI N42.42 (*.n42);;"
        "NPES-JSON (*.json);;"
        "All files (*)");
}

Format formatFromPath(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("tka")) {
        return Format::Tka;
    }
    if (ext == QLatin1String("n42") || ext == QLatin1String("xml")) {
        return Format::N42;
    }
    if (ext == QLatin1String("json")) {
        return Format::Npes;
    }
    if (ext == QLatin1String("csv")) {
        return Format::Csv;
    }
    return Format::Csv;
}

QString readSpectrumFile(const QString &path, QtRadiacode::RcSpectrum *out)
{
    if (!out) {
        return QStringLiteral("Internal error: null spectrum output");
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QStringLiteral("Cannot read file: %1").arg(path);
    }
    const QByteArray raw = f.readAll();
    f.close();
    if (raw.isEmpty()) {
        return QStringLiteral("File is empty: %1").arg(path);
    }

    Format format = formatFromPath(path);
    // Unknown / wrong extension: sniff content.
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext.isEmpty() || ext == QLatin1String("txt") || ext == QLatin1String("dat")
        || (ext != QLatin1String("csv") && ext != QLatin1String("tka")
            && ext != QLatin1String("n42") && ext != QLatin1String("json")
            && ext != QLatin1String("xml"))) {
        format = guessFormatFromContent(raw);
    }

    QtRadiacode::RcSpectrum sp;
    QString err;
    bool ok = false;
    switch (format) {
    case Format::Csv:
        ok = parseCsv(QString::fromUtf8(raw), &sp, &err);
        break;
    case Format::Tka:
        ok = parseTka(QString::fromUtf8(raw), &sp, &err);
        break;
    case Format::N42:
        ok = parseN42(QString::fromUtf8(raw), &sp, &err);
        break;
    case Format::Npes:
        ok = parseNpes(raw, &sp, &err);
        break;
    }

    // If extension-based parse failed, try content sniff once.
    if (!ok) {
        const Format guess = guessFormatFromContent(raw);
        if (guess != format) {
            err.clear();
            switch (guess) {
            case Format::Csv:
                ok = parseCsv(QString::fromUtf8(raw), &sp, &err);
                break;
            case Format::Tka:
                ok = parseTka(QString::fromUtf8(raw), &sp, &err);
                break;
            case Format::N42:
                ok = parseN42(QString::fromUtf8(raw), &sp, &err);
                break;
            case Format::Npes:
                ok = parseNpes(raw, &sp, &err);
                break;
            }
        }
    }

    if (!ok) {
        return err.isEmpty() ? QStringLiteral("Failed to parse spectrum file") : err;
    }
    *out = sp;
    return {};
}

} // namespace SpectrumExport
