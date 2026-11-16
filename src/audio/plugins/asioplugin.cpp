/**
 *  OSM
 *  Copyright (C) 2021  Pavel Smokotnin

 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "asioplugin.h"
#include <future>
#include <limits>
#include <QMetaType>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

// The SDK's ASIOExit() releases the driver through this global (asio.cpp). The SDK's
// own loadAsioDriver() would create it; OSM keeps its own AsioDrivers instead, so it
// has to point the global at it, or ASIOExit() dereferences a null pointer.
extern AsioDrivers *asioDrivers;

namespace audio {

namespace asioCallbacks {
static ASIOPlugin *currentPlugin = nullptr;
}

bool checkASIOStatus(ASIOError result, QString message, std::list<ASIOError> goodStatuses = {ASE_OK});
# define checkASIOCall(foo, returnval, message) \
    if (!checkASIOStatus(foo, QString(message) + " " + QString::number(__LINE__))) {\
    return returnval;\
};

ASIOPlugin::ASIOPlugin() : Plugin(), m_bufferSize(0),
    m_workingThread(this), m_currentDevice(),
    m_drivers(), m_deviceList(),
    m_outputCallbacks(), m_inputCallbacks(),
    m_bufferInfo(), m_currentChannelInfo(), m_inputBuffer(), m_outputBuffer()
{
    if (asioCallbacks::currentPlugin != nullptr) {
        qFatal("ASIO plugin can't be created more than once");
        return;
    }
    asioCallbacks::currentPlugin = this;
    asioDrivers = &m_drivers;

    qRegisterMetaType<const QVector<float> &>();
    connect(this, &ASIOPlugin::runInputProcessing, this, [this](const QVector<float> &buffer) {
        processInputStreams(buffer);
    });
    moveToThread(&m_workingThread);
    connect(&m_workingThread, &QThread::started, this, &ASIOPlugin::loadDeviceList, Qt::DirectConnection);

    m_workingThread.setObjectName("ASIO thread");
    m_workingThread.start();
    m_workingThread.setPriority(QThread::TimeCriticalPriority);
}

ASIOPlugin::~ASIOPlugin()
{
    m_workingThread.quit();
    m_workingThread.wait();
    stopCurrentDevice();
    asioCallbacks::currentPlugin = nullptr;
    if (asioDrivers == &m_drivers) {
        asioDrivers = nullptr;
    }
}

QString ASIOPlugin::name() const
{
    return "ASIO";
}

DeviceInfo::List ASIOPlugin::getDeviceInfoList() const
{
    return m_deviceList;
}

namespace {

constexpr long MAX_PROBE_CHANNELS = 192;
constexpr int ASIO_NAME_SIZE = 32;

//! everything learned about one driver, plain data so it can live inside __try
struct DriverProbe {
    char name[ASIO_NAME_SIZE];
    double sampleRate;
    long inputCount, outputCount;
    char inputNames[MAX_PROBE_CHANNELS][ASIO_NAME_SIZE];
    char outputNames[MAX_PROBE_CHANNELS][ASIO_NAME_SIZE];
};

enum ProbeResult { ProbeOk, ProbeLoadFailed, ProbeInitFailed, ProbeSampleRateFailed, ProbeChannelsFailed, ProbeCrashed };

/**
 * Load one driver and read its sample rate and channels. Third party drivers
 * sometimes crash when queried; on Windows the probe runs inside a structured
 * exception handler, so a faulty driver is skipped instead of taking the app down.
 * No C++ objects with destructors may live in this function (SEH restriction).
 */
ProbeResult probeDriverUnsafe(AsioDrivers *drivers, DriverProbe *probe)
{
    if (!drivers->loadDriver(probe->name)) {
        return ProbeLoadFailed;
    }
    ASIODriverInfo driverInfo = {2L, 0, "", "", nullptr};
    if (ASIOInit(&driverInfo) != ASE_OK) {
        drivers->removeCurrentDriver();
        return ProbeInitFailed;
    }
    ASIOSampleRate sr;
    if (ASIOGetSampleRate(&sr) != ASE_OK) {
        ASIOExit();
        drivers->removeCurrentDriver();
        return ProbeSampleRateFailed;
    }
    probe->sampleRate = sr;

    long inputCount = 0, outputCount = 0;
    if (ASIOGetChannels(&inputCount, &outputCount) != ASE_OK) {
        ASIOExit();
        drivers->removeCurrentDriver();
        return ProbeChannelsFailed;
    }
    if (inputCount < 0 || inputCount > MAX_PROBE_CHANNELS) {
        inputCount = 0;
    }
    if (outputCount < 0 || outputCount > MAX_PROBE_CHANNELS) {
        outputCount = 0;
    }
    probe->inputCount = inputCount;
    probe->outputCount = outputCount;

    for (long j = 0; j < inputCount; ++j) {
        ASIOChannelInfo channelInfo;
        channelInfo.channel = j;
        channelInfo.name[0] = '\0';
        channelInfo.isInput = ASIOTrue;
        channelInfo.type = ASIOSTFloat32LSB;
        ASIOGetChannelInfo(&channelInfo);
        memcpy(probe->inputNames[j], channelInfo.name, ASIO_NAME_SIZE);
        probe->inputNames[j][ASIO_NAME_SIZE - 1] = '\0';
    }
    for (long j = 0; j < outputCount; ++j) {
        ASIOChannelInfo channelInfo;
        channelInfo.channel = j;
        channelInfo.name[0] = '\0';
        channelInfo.isInput = ASIOFalse;
        ASIOGetChannelInfo(&channelInfo);
        memcpy(probe->outputNames[j], channelInfo.name, ASIO_NAME_SIZE);
        probe->outputNames[j][ASIO_NAME_SIZE - 1] = '\0';
    }
    ASIOExit();
    drivers->removeCurrentDriver();
    return ProbeOk;
}

ProbeResult probeDriver(AsioDrivers *drivers, DriverProbe *probe)
{
#ifdef _MSC_VER
    __try {
        return probeDriverUnsafe(drivers, probe);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return ProbeCrashed;
    }
#else
    return probeDriverUnsafe(drivers, probe);
#endif
}

long driverNamesUnsafe(AsioDrivers *drivers, char **names, long max)
{
    return drivers->getDriverNames(names, max);
}

long driverNames(AsioDrivers *drivers, char **names, long max)
{
#ifdef _MSC_VER
    __try {
        return driverNamesUnsafe(drivers, names, max);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
#else
    return driverNamesUnsafe(drivers, names, max);
#endif
}

void releaseDriver(AsioDrivers *drivers)
{
#ifdef _MSC_VER
    __try {
        drivers->removeCurrentDriver();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#else
    drivers->removeCurrentDriver();
#endif
}

const char *probeResultName(ProbeResult result)
{
    switch (result) {
    case ProbeOk:               return "ok";
    case ProbeLoadFailed:       return "could not be loaded";
    case ProbeInitFailed:       return "ASIOInit failed";
    case ProbeSampleRateFailed: return "ASIOGetSampleRate failed";
    case ProbeChannelsFailed:   return "ASIOGetChannels failed";
    case ProbeCrashed:          return "crashed while being queried and was skipped";
    }
    return "unknown";
}

} // namespace

void ASIOPlugin::loadDeviceList()
{
    if (!m_currentDevice.isNull()) {
        return;
    }
    CoInitialize(NULL);
    DeviceInfo::List list = {};
    auto that = const_cast<ASIOPlugin *>(this);

    const long count = m_drivers.numdrv > 0 ? static_cast<long>(m_drivers.numdrv) : 0L;
    std::vector<std::array<char, 256>> nameBuffers(static_cast<size_t>(count));
    std::vector<char *> names(static_cast<size_t>(count));
    for (long i = 0; i < count; ++i) {
        nameBuffers[i].fill('\0');
        names[i] = nameBuffers[i].data();
    }
    const long found = count ? driverNames(&that->m_drivers, names.data(), count) : 0;
    qInfo() << "ASIO: found" << found << "driver(s)";
    if (found < 0) {
        qWarning() << "ASIO: reading the driver list crashed; ASIO devices are not available";
    }

    auto probe = std::make_unique<DriverProbe>();
    for (long i = 0; i < found; ++i) {
        std::memset(probe.get(), 0, sizeof(DriverProbe));
        std::strncpy(probe->name, names[i], ASIO_NAME_SIZE - 1);
        qInfo() << "ASIO: probing" << probe->name;

        const auto result = probeDriver(&that->m_drivers, probe.get());
        if (result != ProbeOk) {
            qWarning() << "ASIO:" << probe->name << probeResultName(result);
            if (result == ProbeCrashed) {
                // the driver may be half initialised; let go of it
                releaseDriver(&m_drivers);
            }
            continue;
        }
        if (!probe->inputCount && !probe->outputCount) {
            qInfo() << "ASIO:" << probe->name << "has no channels";
            continue;
        }

        DeviceInfo info(probe->name, name());
        info.setName(probe->name);
        info.setDefaultSampleRate(static_cast<unsigned int>(probe->sampleRate));
        QStringList inputs = {}, outputs = {};
        for (long j = 0; j < probe->inputCount; ++j) {
            inputs << (probe->inputNames[j][0] != '\0' ? QString(probe->inputNames[j]) : QString::number(j));
        }
        for (long j = 0; j < probe->outputCount; ++j) {
            outputs << (probe->outputNames[j][0] != '\0' ? QString(probe->outputNames[j]) : QString::number(j));
        }
        info.setInputChannels(inputs);
        info.setOutputChannels(outputs);
        qInfo() << "ASIO:" << probe->name << "ok," << probe->inputCount << "in /" << probe->outputCount << "out at"
                << probe->sampleRate;
        list << info;
    }

    if (m_deviceList != list) {
        m_deviceList = list;
        emit deviceListChanged();
    }
    CoUninitialize();
}

DeviceInfo ASIOPlugin::deviceInfo(const DeviceInfo::Id &id) const
{
    for (auto &device : m_deviceList) {
        if (device.id() == id) {
            return device;
        }
    }
    return {};
}

DeviceInfo::Id ASIOPlugin::defaultDeviceId(const Plugin::Direction &) const
{
    return DeviceInfo::Id();
}

Format ASIOPlugin::deviceFormat(const DeviceInfo::Id &id, const Plugin::Direction &mode) const
{
    auto device = deviceInfo(id);
    unsigned int count = (mode == Input ? device.inputChannels().count() : device.outputChannels().count());

    return {
        device.defaultSampleRate(),
        count
    };
}

Stream *ASIOPlugin::open(const DeviceInfo::Id &id, const Plugin::Direction &mode, const Format &, QIODevice *endpoint)
{
    if (!startDevice(id)) {
        stopCurrentDevice();
        return nullptr;
    }

    Format streamFormat = deviceFormat(id, mode);
    auto *stream  = new Stream(streamFormat);
    stream->moveToThread(&m_workingThread);

    connect(stream, &Stream::closeMe, this, [endpoint]() {
        if (endpoint->isOpen()) {
            endpoint->close();
        }
        //don't delete Stream here
        //ASIO driver will safely remove inactive stream from callbacks and then schedule it for deletion
    }, Qt::DirectConnection);

    switch (mode) {
    case Input:
        endpoint->open(QIODevice::WriteOnly);
        m_inputCallbacks[stream] = [endpoint, this](QVector<float> buffer) {
            if (endpoint->isWritable()) {
                endpoint->write(reinterpret_cast<char *>(buffer.data()), m_inputBuffer.size() * sizeof (float));
            }
        };
        break;

    case Output:
        endpoint->open(QIODevice::ReadOnly);
        m_outputCallbacks[stream] = [endpoint, this]() {
            if (endpoint->isReadable()) {
                endpoint->read(reinterpret_cast<char *>(m_outputBuffer.data()), m_outputBuffer.size() * sizeof (float));
            }
        };
        break;
    }

    return stream;
}

bool ASIOPlugin::startDevice(const DeviceInfo::Id &id)
{
    if (m_currentDevice == id) {
        return true;
    }
    CoInitialize(NULL);
    stopCurrentDevice();
    m_currentDevice = id;

    QByteArray ch = m_currentDevice.toLocal8Bit();
    if (!m_drivers.loadDriver(ch.data())) {
        m_currentDevice = DeviceInfo::Id();
        qCritical() << "can't load driver " << id;
        return false;
    }

    ASIODriverInfo driverInfo = {2L, 0, "", "", nullptr};
    auto initReult = ASIOInit(&driverInfo);
    checkASIOCall(initReult, false, "ASIOInit " + m_currentDevice);

    struct bufferSizes {
        long min;
        long max;
        long preferd;
        long granularity;
    } bufferSizes;
    checkASIOCall(ASIOGetBufferSize(&bufferSizes.min, &bufferSizes.max, &bufferSizes.preferd, &bufferSizes.granularity),
                  false, "ASIOGetBufferSize");

    auto sampleRate = deviceInfo(m_currentDevice).defaultSampleRate();
    checkASIOCall(ASIOSetSampleRate(sampleRate), false, "ASIOSetSampleRate");

    auto device = deviceInfo(m_currentDevice);
    auto channelCount = device.inputChannels().count() + device.outputChannels().count();

    m_bufferSize = bufferSizes.preferd;
    m_bufferInfo.resize(channelCount);
    m_currentChannelInfo.resize(channelCount);
    m_inputBuffer.resize(device.inputChannels().count() * m_bufferSize);
    m_outputBuffer.resize(device.outputChannels().count() * m_bufferSize);

    for (int i = 0; i < device.inputChannels().count(); ++i) {
        m_bufferInfo[i].isInput = ASIOTrue;
        m_bufferInfo[i].channelNum = i;
        m_currentChannelInfo[i].channel = i;
        m_currentChannelInfo[i].isInput = ASIOTrue;
        m_currentChannelInfo[i].name[0] = '\0';
        ASIOGetChannelInfo(&m_currentChannelInfo[i]);
    }
    for (int i = device.inputChannels().count(), j = 0; i < channelCount; ++i, ++j) {
        m_bufferInfo[i].isInput = ASIOFalse;
        m_bufferInfo[i].channelNum = j;
        m_currentChannelInfo[i].channel = j;
        m_currentChannelInfo[i].isInput = ASIOFalse;
        m_currentChannelInfo[i].name[0] = '\0';
        ASIOGetChannelInfo(&m_currentChannelInfo[i]);
    }

    static ASIOCallbacks callbacks;
    callbacks.bufferSwitch          = &asioCallbacks::bufferSwitch;
    callbacks.sampleRateDidChange   = &asioCallbacks::sampleRateDidChange;
    callbacks.asioMessage           = &asioCallbacks::asioMessage;
    callbacks.bufferSwitchTimeInfo  = &asioCallbacks::bufferSwitchTimeInfo;

    checkASIOCall(ASIOCreateBuffers(m_bufferInfo.data(), channelCount, m_bufferSize, &callbacks), false,
                  "ASIOCreateBuffers");

    ASIOOutputReady();
    checkASIOCall(ASIOStart(), false, "ASIOStart");

    return true;
}

void ASIOPlugin::stopCurrentDevice()
{
    CoInitialize(NULL);
    ASIOStop();
    ASIODisposeBuffers();
    ASIOExit();
    stopAllStreams();
    m_currentDevice = DeviceInfo::Id();
}

void ASIOPlugin::stopAllStreams()
{
    for (auto &&stream : m_inputCallbacks.keys()) {
        stream->close();
    }
    for (auto &&stream : m_outputCallbacks.keys()) {
        stream->close();
    }
}

void ASIOPlugin::processInputStreams(const QVector<float> &buffer)
{
    for (auto it = m_inputCallbacks.begin(); it != m_inputCallbacks.end();) {
        if (Q_UNLIKELY(!it.key()->active())) {
            it.key()->deleteLater();
            it = m_inputCallbacks.erase(it);
        } else {
            it.value()(buffer);
            ++it;
        }
    }
}

void ASIOPlugin::processOutputStreams()
{
    if (!m_outputCallbacks.size()) {
        std::fill(m_outputBuffer.begin(), m_outputBuffer.end(), 0.f);
        return;
    }
    for (auto it = m_outputCallbacks.begin(); it != m_outputCallbacks.end();) {
        if (Q_UNLIKELY(!it.key()->active())) {
            it.key()->deleteLater();
            it = m_outputCallbacks.erase(it);
        } else {
            it.value()();
            ++it;
        }
    }
}

void ASIOPlugin::sampleRateDidChange(ASIOSampleRate sampleRate)
{
    for (auto &&stream : m_inputCallbacks.keys()) {
        stream->setSampleRate(sampleRate);
    }
    for (auto &&stream : m_outputCallbacks.keys()) {
        stream->setSampleRate(sampleRate);
    }
}

struct INT24 {
    unsigned char m_data[3] = {0, 0, 0};
    INT24() {}
    INT24(const double &from)
    {
        INT32 iFrom = static_cast<INT32>(from);
        m_data[0] = reinterpret_cast<unsigned char *>(&iFrom)[0];
        m_data[1] = reinterpret_cast<unsigned char *>(&iFrom)[1];
        m_data[2] = reinterpret_cast<unsigned char *>(&iFrom)[2];
    }

    operator double()
    {
        INT32 to = 0;
        memcpy(&to, m_data, 3);
        return to;
    }
};

constexpr unsigned long cpow(unsigned int b, unsigned int e)
{
    unsigned long r = 1;
    while (e > 0) {
        r *= b;
        --e;
    }
    return r;
}
template<typename T> inline void setSample(void *data, const float *value, size_t channelNumber, size_t sampleNumber,
                                           size_t channelsPerFrame)
{
    constexpr unsigned long bound = cpow(2, sizeof(T) * 8 - 1) - 1;
    double v = *(value + channelNumber + sampleNumber * channelsPerFrame);
    T *pData = reinterpret_cast<T *>(data) + sampleNumber;
    T sample = static_cast<T>(v * bound);
    memmove(pData, &sample, sizeof(T));
}
template<> inline void setSample<float>(void *data, const float *value, size_t channelNumber, size_t sampleNumber,
                                        size_t channelsPerFrame)
{
    const float *sample = value + channelNumber + sampleNumber * channelsPerFrame;
    float *pData = reinterpret_cast<float *>(data) + sampleNumber;
    memmove(pData, sample, sizeof(float));
}
template<> inline void setSample<double>(void *data, const float *value, size_t channelNumber, size_t sampleNumber,
                                         size_t channelsPerFrame)
{
    double sample = *(value + channelNumber + sampleNumber * channelsPerFrame);
    float *pData = reinterpret_cast<float *>(data) + sampleNumber;
    memmove(pData, &sample, sizeof(float));
}
template<typename T> inline void getSample(void *data, float *value, size_t channelNumber, size_t sampleNumber,
                                           size_t channelsPerFrame)
{
    constexpr unsigned long bound = cpow(2, sizeof(T) * 8 - 1) - 1;
    T *pData = reinterpret_cast<T *>(data) + sampleNumber;
    float v = static_cast<double>(*pData) / static_cast<double>(bound);
    if (v > 1) {
        v -= 2;
    }
    memmove(value + channelNumber + sampleNumber * channelsPerFrame, &v, sizeof(float));
}
template<> inline void getSample<float>(void *data, float *value, size_t channelNumber, size_t sampleNumber,
                                        size_t channelsPerFrame)
{
    float *pData = reinterpret_cast<float *>(data) + sampleNumber;
    memcpy(value + channelNumber + sampleNumber * channelsPerFrame, pData, sizeof(float));
}
template<> inline void getSample<double>(void *data, float *value, size_t channelNumber, size_t sampleNumber,
                                         size_t channelsPerFrame)
{
    double *pData = reinterpret_cast<double *>(data) + sampleNumber;
    float v = static_cast<float>(*pData);
    memmove(value + channelNumber + sampleNumber * channelsPerFrame, &v, sizeof(float));
}
ASIOTime *ASIOPlugin::processBuffers(ASIOTime *params, long doubleBufferIndex, ASIOBool directProcess)
{
    if (!directProcess) {
        return nullptr;
    }

    if (!(params->timeInfo.flags & (kSystemTimeValid | kSamplePositionValid | kSampleRateValid | kSpeedValid))) {
        return params;
    }

    processOutputStreams();
    auto outputCount = m_outputBuffer.size() / m_bufferSize;
    auto inputCount  = m_inputBuffer.size()  / m_bufferSize;
    for (int i = 0; i < m_bufferInfo.size(); ++i) {
        void *pASIOBuffer = m_bufferInfo[i].buffers[doubleBufferIndex];
        for (size_t j = 0; j < m_bufferSize; ++j) {
            switch (m_currentChannelInfo[i].isInput) {
            case ASIOTrue:
                switch (m_currentChannelInfo[i].type) {
                case ASIOSTInt16LSB:
                    getSample<INT16>(pASIOBuffer, m_inputBuffer.data(), m_bufferInfo[i].channelNum, j, inputCount);
                    break;
                case ASIOSTInt24LSB:
                    getSample<INT24>(pASIOBuffer, m_inputBuffer.data(), m_bufferInfo[i].channelNum, j, inputCount);
                    break;
                case ASIOSTInt32LSB:
                    getSample<INT32>(pASIOBuffer, m_inputBuffer.data(), m_bufferInfo[i].channelNum, j, inputCount);
                    break;
                case ASIOSTFloat32LSB:
                    getSample<float>(pASIOBuffer, m_inputBuffer.data(), m_bufferInfo[i].channelNum, j, inputCount);
                    break;
                case ASIOSTFloat64LSB:
                    getSample<double>(pASIOBuffer, m_inputBuffer.data(), m_bufferInfo[i].channelNum, j, inputCount);
                    break;
                default:
                    qFatal("unknown sample type");
                    ASIOStop();
                }
                break;

            case ASIOFalse:
                switch (m_currentChannelInfo[i].type) {
                case ASIOSTInt16LSB:
                    setSample<INT16>(pASIOBuffer, m_outputBuffer.data(), m_bufferInfo[i].channelNum, j, outputCount);
                    break;
                case ASIOSTInt24LSB:
                    setSample<INT24>(pASIOBuffer, m_outputBuffer.data(), m_bufferInfo[i].channelNum, j, outputCount);
                    break;
                case ASIOSTInt32LSB:
                    setSample<INT32>(pASIOBuffer, m_outputBuffer.data(), m_bufferInfo[i].channelNum, j, outputCount);
                    break;
                case ASIOSTFloat32LSB:
                    setSample<float>(pASIOBuffer, m_outputBuffer.data(), m_bufferInfo[i].channelNum, j, outputCount);
                    break;
                case ASIOSTFloat64LSB: {
                    setSample<double>(pASIOBuffer, m_outputBuffer.data(), m_bufferInfo[i].channelNum, j, outputCount);
                    break;
                }
                break;
                default:
                    qFatal("unknown sample type %d", m_currentChannelInfo[i].type);
                    ASIOStop();
                }
                break;
            }
        }
    }
    ASIOOutputReady();
    emit runInputProcessing(m_inputBuffer, {});
    return params;
}

namespace asioCallbacks {
void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess)
{
    ASIOTime  timeInfo;
    memset (&timeInfo, 0, sizeof (timeInfo));
    if (checkASIOStatus(ASIOGetSamplePosition(&timeInfo.timeInfo.samplePosition, &timeInfo.timeInfo.systemTime),
                        "ASIOGetSamplePosition")) {
        timeInfo.timeInfo.flags = kSystemTimeValid | kSamplePositionValid;
    }

    bufferSwitchTimeInfo (&timeInfo, doubleBufferIndex, directProcess);
};
void sampleRateDidChange(ASIOSampleRate sRate)
{
    if (currentPlugin) {
        currentPlugin->sampleRateDidChange(sRate);
    }
};
long asioMessage(long selector, long value, void *message, double *opt)
{
    Q_UNUSED(message)
    Q_UNUSED(opt)
    long ret = 0;
    switch (selector) {
    case kAsioSelectorSupported: {
        QList<long> values = {
            kAsioResetRequest,
            kAsioEngineVersion,
            kAsioResyncRequest,
            kAsioLatenciesChanged,
            kAsioSupportsTimeInfo,
            kAsioSupportsTimeCode,
            kAsioSupportsInputMonitor
        };
        if (values.indexOf(value) != -1) {
            return 1;
        }
        break;
    }
    case kAsioResetRequest:
        if (currentPlugin) {
            currentPlugin->stopAllStreams();
        }
        return 1;
    case kAsioResyncRequest:
        return 1;
    case kAsioLatenciesChanged:
        return 1;
    case kAsioEngineVersion:
        return 2;
    case kAsioSupportsTimeInfo:
        return 1;
    case kAsioSupportsTimeCode:
        return 0;
    }
    return ret;
};
ASIOTime *bufferSwitchTimeInfo(ASIOTime *params, long doubleBufferIndex, ASIOBool directProcess)
{
    if (currentPlugin) {
        return currentPlugin->processBuffers(params, doubleBufferIndex, directProcess);
    }
    return nullptr;
};
}
bool checkASIOStatus(ASIOError result, QString message, std::list<ASIOError> goodStatuses)
{
    if (std::find(goodStatuses.begin(), goodStatuses.end(), result) != goodStatuses.end()) {
        return true;
    }
    QString resultDescription;
    switch (result) {
    case ASE_NotPresent:
        resultDescription = "hardware input or output is not present or available";
        break;
    case ASE_HWMalfunction:
        resultDescription = "hardware is malfunctioning (can be returned by any ASIO function)";
        break;
    case ASE_InvalidParameter:
        resultDescription = "input parameter invalid";
        break;
    case ASE_InvalidMode:
        resultDescription = "hardware is in a bad mode or used in a bad mode";
        break;
    case ASE_SPNotAdvancing:
        resultDescription = "hardware is not running when sample position is inquired";
        break;
    case ASE_NoClock:
        resultDescription = "sample clock or rate cannot be determined or is not present";
        break;
    case ASE_NoMemory:
        resultDescription = "not enough memory for completing the request";
        break;
    }

    qCritical() << message << result << resultDescription;
    return false;
}

} // namespace audio
