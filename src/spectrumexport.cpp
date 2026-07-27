#include "spectrumexport.h"

#include "protocol/types.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QUuid>

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

} // namespace SpectrumExport
