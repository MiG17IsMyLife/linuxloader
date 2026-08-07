#include "es1VirtualDevices.h"
#include "es1Jvs.h"
#include "es1Kickback.h"

#include "../../../platform/platformBackend.h"
#include "../../../elfLoader/virtualDeviceRegistry.hpp"
#include "../../common/jvs.h"
#include "../../../input/sdlInput.h"
#include "../../../log/log.h"

#include <array>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <vector>
#include <windows.h>

namespace
{
struct JsEvent
{
    uint32_t time;
    int16_t value;
    uint8_t type;
    uint8_t number;
};

constexpr uint8_t JS_EVENT_BUTTON = 0x01;
constexpr uint8_t JS_EVENT_AXIS = 0x02;
constexpr uint8_t JS_EVENT_INIT = 0x80;

constexpr unsigned long VIDIOC_QUERYCAP = 0x80685600;
constexpr unsigned long VIDIOC_ENUM_FMT = 0xc0405602;
constexpr unsigned long VIDIOC_G_FMT = 0xc0d05604;
constexpr unsigned long VIDIOC_S_FMT = 0xc0cc5605;
constexpr unsigned long VIDIOC_REQBUFS = 0xc0145608;
constexpr unsigned long VIDIOC_QUERYBUF = 0xc0445609;
constexpr unsigned long VIDIOC_QBUF = 0xc044560f;
constexpr unsigned long VIDIOC_DQBUF = 0xc0445611;
constexpr unsigned long VIDIOC_STREAMON = 0x40045612;
constexpr unsigned long VIDIOC_STREAMOFF = 0x40045613;
/* Private controls issued by the ES1 camera wrapper during startup. */
constexpr unsigned long VIDIOC_ES1_CONTROL = 0xc02c563a;
constexpr unsigned long VIDIOC_ES1_CONTROL_SET = 0x4014563c;

constexpr uint32_t V4L2_BUF_TYPE_VIDEO_CAPTURE = 1;
constexpr uint32_t V4L2_MEMORY_MMAP = 1;
constexpr uint32_t V4L2_FIELD_NONE = 1;
constexpr uint32_t V4L2_PIX_FMT_YUYV = 0x56595559;

#pragma pack(push, 1)
struct V4l2Capability
{
    char driver[16];
    char card[32];
    char busInfo[32];
    uint32_t version;
    uint32_t capabilities;
    uint32_t deviceCaps;
    uint32_t reserved[3];
};

struct V4l2PixFormat
{
    uint32_t width;
    uint32_t height;
    uint32_t pixelFormat;
    uint32_t field;
    uint32_t bytesPerLine;
    uint32_t sizeImage;
    uint32_t colorspace;
    uint32_t priv;
    uint32_t flags;
    uint32_t ycbcrEncoding;
    uint32_t quantization;
    uint32_t transferFunction;
};

struct V4l2Format
{
    uint32_t type;
    V4l2PixFormat pix;
};

struct V4l2Rect
{
    int32_t left;
    int32_t top;
    int32_t width;
    int32_t height;
};

struct V4l2CropCap
{
    uint32_t type;
    V4l2Rect bounds;
    V4l2Rect defaultRectangle;
    uint32_t pixelAspectNumerator;
    uint32_t pixelAspectDenominator;
};

struct V4l2RequestBuffers
{
    uint32_t count;
    uint32_t type;
    uint32_t memory;
    uint32_t reserved[2];
};

struct V4l2Buffer
{
    uint32_t index;
    uint32_t type;
    uint32_t bytesUsed;
    uint32_t flags;
    uint32_t field;
    uint32_t timestamp[2];
    uint32_t timecode[4];
    uint32_t sequence;
    uint32_t memory;
    uint32_t offset;
    uint32_t length;
    uint32_t reserved2;
    uint32_t reserved;
};
#pragma pack(pop)

enum class Kind
{
    Serial,
    Joystick,
    Camera
};

struct Slot
{
    int fd = -1;
    Kind kind = Kind::Serial;
    bool used = false;
    std::vector<unsigned char> input;
    std::vector<unsigned char> output;
    unsigned char joystickAxis = 0;
    unsigned char joystickButton = 0;
    bool joystickInitialized = false;
    short joystickAxes[4]{};
    unsigned char joystickButtons[16]{};
    bool cameraStreaming = false;
    uint32_t cameraWidth = 640;
    uint32_t cameraHeight = 480;
    uint32_t cameraImageSize = 640 * 480 * 2;
    uint32_t cameraNextBuffer = 0;
    std::vector<void *> cameraMappings;
    std::vector<size_t> cameraMappingLengths;
};

std::array<Slot, 8> slots{};
std::mutex slotsMutex;
constexpr int FirstDescriptor = 0x4e10;

bool claims(const char *path)
{
    if (!platformIsES1() || !path)
        return false;
    return std::strcmp(path, "/dev/input/js0") == 0 ||
           std::strcmp(path, "/dev/video0") == 0;
}

Kind kindForPath(const char *path)
{
    if (std::strncmp(path, "/dev/ttyS", 9) == 0)
        return Kind::Serial;
    if (std::strcmp(path, "/dev/input/js0") == 0)
        return Kind::Joystick;
    return Kind::Camera;
}

int openDevice(const char *path, int flags)
{
    (void)flags;
    std::lock_guard<std::mutex> lock(slotsMutex);
    for (Slot &slot : slots)
    {
        if (!slot.used)
        {
            slot.used = true;
            slot.kind = kindForPath(path);
            slot.input.clear();
            slot.output.clear();
            slot.joystickInitialized = false;
            slot.joystickAxis = 0;
            slot.joystickButton = 0;
            std::fill(std::begin(slot.joystickAxes), std::end(slot.joystickAxes), 0);
            std::fill(std::begin(slot.joystickButtons), std::end(slot.joystickButtons), 0);
            slot.cameraStreaming = false;
            slot.cameraNextBuffer = 0;
            slot.cameraMappings.clear();
            slot.cameraMappingLengths.clear();
            slot.fd = FirstDescriptor + static_cast<int>(&slot - slots.data());
            log_info("System ES1: opened virtual device %s as fd %d", path, slot.fd);
            return slot.fd;
        }
    }
    errno = EMFILE;
    return -1;
}

Slot *findSlot(int fd)
{
    for (Slot &slot : slots)
    {
        if (slot.used && slot.fd == fd)
            return &slot;
    }
    return nullptr;
}

int owns(int fd)
{
    std::lock_guard<std::mutex> lock(slotsMutex);
    return findSlot(fd) ? 1 : 0;
}

int available(int fd)
{
    std::lock_guard<std::mutex> lock(slotsMutex);
    Slot *slot = findSlot(fd);
    if (!slot)
        return 0;
    if (slot->kind == Kind::Joystick)
        return 1;
    if (slot->kind == Kind::Camera && slot->cameraStreaming)
        return 1;
    return static_cast<int>(slot->output.size());
}

bool extractJvsFrame(Slot &slot, std::vector<unsigned char> &frame)
{
    while (!slot.input.empty() && slot.input.front() != SYNC)
        slot.input.erase(slot.input.begin());

    if (slot.input.size() < 3)
        return false;

    std::vector<unsigned char> decoded;
    decoded.reserve(JVS_MAX_PACKET_SIZE);
    bool escaped = false;
    size_t consumed = 1;
    for (size_t i = 1; i < slot.input.size(); ++i)
    {
        unsigned char value = slot.input[i];
        ++consumed;
        if (!escaped && value == ESCAPE)
        {
            escaped = true;
            continue;
        }
        if (escaped)
        {
            value = static_cast<unsigned char>(value + 1);
            escaped = false;
        }
        decoded.push_back(value);
        if (decoded.size() == 2)
        {
            const size_t expected = 2 + decoded[1];
            if (expected > JVS_MAX_PACKET_SIZE + 2)
            {
                slot.input.erase(slot.input.begin());
                return false;
            }
        }
        if (decoded.size() >= 2 && decoded.size() == static_cast<size_t>(2 + decoded[1]))
        {
            frame.assign(slot.input.begin(), slot.input.begin() + consumed);
            slot.input.erase(slot.input.begin(), slot.input.begin() + consumed);
            return true;
        }
    }
    return false;
}

void processJvsFrames(Slot &slot)
{
    std::vector<unsigned char> frame;
    while (extractJvsFrame(slot, frame))
    {
        if (frame.size() > sizeof(inputBuffer))
            continue;
        std::memcpy(inputBuffer, frame.data(), frame.size());

        int responseSize = 0;
        if (processPacket(&responseSize) == JVS_STATUS_SUCCESS && responseSize > 0)
            slot.output.insert(slot.output.end(), outputBuffer, outputBuffer + responseSize);
        frame.clear();
    }
}

int readDevice(int fd, void *buffer, size_t count)
{
    std::lock_guard<std::mutex> lock(slotsMutex);
    Slot *slot = findSlot(fd);
    if (!slot || !buffer)
    {
        errno = EBADF;
        return -1;
    }
    if (slot->kind == Kind::Serial)
    {
        if (slot->output.empty())
        {
            errno = EAGAIN;
            return -1;
        }
        const size_t amount = std::min(count, slot->output.size());
        std::memcpy(buffer, slot->output.data(), amount);
        slot->output.erase(slot->output.begin(), slot->output.begin() + amount);
        return static_cast<int>(amount);
    }
    if (slot->kind == Kind::Joystick && count >= 8)
    {
        static const LogicalAction axes[] = {LA_Steer, LA_Gas, LA_Brake, LA_Throttle};
        static const LogicalAction buttons[] = {
            LA_Start, LA_Coin, LA_Service, LA_ViewChange, LA_Boost, LA_GearUp, LA_GearDown,
            LA_Button1, LA_Button2, LA_Button3, LA_Button4, LA_Button5, LA_Button6, LA_Button7,
            LA_Button8, LA_CardInsert};

        JsEvent event{};
        bool changed = false;
        const bool initializing = !slot->joystickInitialized;
        if (!slot->joystickInitialized)
        {
            slot->joystickInitialized = true;
            slot->joystickAxis = 0;
            slot->joystickButton = 0;
        }

        if (slot->joystickAxis < sizeof(axes) / sizeof(axes[0]))
        {
            const float value = gActionStates[PLAYER_1][axes[slot->joystickAxis]].analogValue;
            const short current = static_cast<short>(std::clamp(value, 0.0f, 1.0f) * 65535.0f - 32768.0f);
            if (current != slot->joystickAxes[slot->joystickAxis] || initializing)
            {
                slot->joystickAxes[slot->joystickAxis] = current;
                event.type = JS_EVENT_AXIS | (initializing ? JS_EVENT_INIT : 0);
                event.number = slot->joystickAxis++;
                event.value = current;
                changed = true;
            }
        }

        if (!changed && slot->joystickAxis >= sizeof(axes) / sizeof(axes[0]) &&
            slot->joystickButton < sizeof(buttons) / sizeof(buttons[0]))
        {
            const unsigned char current = gActionStates[PLAYER_1][buttons[slot->joystickButton]].isActive ? 1 : 0;
            if (current != slot->joystickButtons[slot->joystickButton] ||
                slot->joystickButton == 0)
            {
                slot->joystickButtons[slot->joystickButton] = current;
                event.type = JS_EVENT_BUTTON | (initializing ? JS_EVENT_INIT : 0);
                event.number = slot->joystickButton++;
                event.value = current;
                changed = true;
            }
        }

        if (changed)
        {
            std::memcpy(buffer, &event, sizeof(event));
            return sizeof(event);
        }
    }
    errno = EAGAIN;
    return -1;
}

int writeDevice(int fd, const void *buffer, size_t count)
{
    std::lock_guard<std::mutex> lock(slotsMutex);
    Slot *slot = findSlot(fd);
    if (!slot || !buffer)
    {
        errno = EBADF;
        return -1;
    }
    if (slot->kind == Kind::Serial)
    {
        const unsigned char *bytes = static_cast<const unsigned char *>(buffer);
        slot->input.insert(slot->input.end(), bytes, bytes + count);
        processJvsFrames(*slot);
    }
    return static_cast<int>(count);
}

int closeDevice(int fd)
{
    std::lock_guard<std::mutex> lock(slotsMutex);
    Slot *slot = findSlot(fd);
    if (!slot)
    {
        errno = EBADF;
        return -1;
    }
    slot->used = false;
    slot->fd = -1;
    slot->input.clear();
    slot->output.clear();
    for (size_t i = 0; i < slot->cameraMappings.size(); ++i)
    {
        if (slot->cameraMappings[i])
            VirtualFree(slot->cameraMappings[i], 0, MEM_RELEASE);
    }
    slot->cameraMappings.clear();
    slot->cameraMappingLengths.clear();
    slot->cameraStreaming = false;
    return 0;
}

void fillCameraFrame(void *mapping, size_t length, uint32_t width, uint32_t height)
{
    if (!mapping || length < static_cast<size_t>(width) * height * 2)
        return;

    unsigned char *pixels = static_cast<unsigned char *>(mapping);
    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; x += 2)
        {
            const size_t offset = (static_cast<size_t>(y) * width + x) * 2;
            const unsigned char y0 = static_cast<unsigned char>(32 + ((x * 160) / width));
            const unsigned char y1 = static_cast<unsigned char>(32 + ((y * 160) / height));
            const unsigned char u = static_cast<unsigned char>(96 + ((y * 64) / height));
            const unsigned char v = static_cast<unsigned char>(128 + ((x * 64) / width));
            pixels[offset + 0] = y0;
            pixels[offset + 1] = u;
            pixels[offset + 2] = y1;
            pixels[offset + 3] = v;
        }
    }
}

int cameraIoctl(Slot &slot, unsigned long request, void *argument)
{
    if (!argument)
    {
        errno = EFAULT;
        return -1;
    }

    if (request == VIDIOC_QUERYCAP)
    {
        auto *capability = static_cast<V4l2Capability *>(argument);
        std::memset(capability, 0, sizeof(*capability));
        std::strncpy(capability->driver, "pacloader", sizeof(capability->driver) - 1);
        std::strncpy(capability->card, "System ES1 Camera", sizeof(capability->card) - 1);
        std::strncpy(capability->busInfo, "platform:es1", sizeof(capability->busInfo) - 1);
        capability->version = 1;
        capability->capabilities = 0x04000001; // VIDEO_CAPTURE | STREAMING
        capability->deviceCaps = capability->capabilities;
        return 0;
    }

    if (request == VIDIOC_ENUM_FMT)
    {
        /* The game only needs a single capture format. */
        struct FormatDescription
        {
            uint32_t index;
            uint32_t type;
            uint32_t pixelFormat;
            char description[32];
            uint32_t flags;
            uint32_t reserved[4];
        };
        auto *format = static_cast<FormatDescription *>(argument);
        if (format->index != 0)
        {
            errno = EINVAL;
            return -1;
        }
        format->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format->pixelFormat = V4L2_PIX_FMT_YUYV;
        std::memset(format->description, 0, sizeof(format->description));
        std::strncpy(format->description, "YUYV 4:2:2", sizeof(format->description) - 1);
        return 0;
    }

    if (request == VIDIOC_ES1_CONTROL || request == VIDIOC_ES1_CONTROL_SET)
    {
        if (request == VIDIOC_ES1_CONTROL)
        {
            auto *crop = static_cast<V4l2CropCap *>(argument);
            std::memset(crop, 0, sizeof(*crop));
            crop->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            crop->bounds.width = static_cast<int32_t>(slot.cameraWidth);
            crop->bounds.height = static_cast<int32_t>(slot.cameraHeight);
            crop->defaultRectangle = crop->bounds;
            crop->pixelAspectNumerator = 1;
            crop->pixelAspectDenominator = 1;
        }
        /* VIDIOC_S_CROP is accepted because the fixed virtual camera already
         * exposes the complete 640x480 capture rectangle. */
        return 0;
    }

    if (request == VIDIOC_S_FMT || request == VIDIOC_G_FMT)
    {
        auto *format = static_cast<V4l2Format *>(argument);
        format->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format->pix.width = slot.cameraWidth;
        format->pix.height = slot.cameraHeight;
        format->pix.pixelFormat = V4L2_PIX_FMT_YUYV;
        format->pix.field = V4L2_FIELD_NONE;
        format->pix.bytesPerLine = slot.cameraWidth * 2;
        format->pix.sizeImage = slot.cameraImageSize;
        return 0;
    }

    if (request == VIDIOC_REQBUFS)
    {
        auto *requestBuffers = static_cast<V4l2RequestBuffers *>(argument);
        if (requestBuffers->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
            requestBuffers->memory != V4L2_MEMORY_MMAP)
        {
            errno = EINVAL;
            return -1;
        }
        requestBuffers->count = 2;
        return 0;
    }

    if (request == VIDIOC_QUERYBUF)
    {
        auto *buffer = static_cast<V4l2Buffer *>(argument);
        if (buffer->index >= 2)
        {
            errno = EINVAL;
            return -1;
        }
        buffer->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer->memory = V4L2_MEMORY_MMAP;
        buffer->length = slot.cameraImageSize;
        buffer->offset = buffer->index * slot.cameraImageSize;
        buffer->bytesUsed = slot.cameraImageSize;
        return 0;
    }

    if (request == VIDIOC_QBUF)
        return 0;

    if (request == VIDIOC_STREAMON)
    {
        if (*static_cast<uint32_t *>(argument) != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        {
            errno = EINVAL;
            return -1;
        }
        slot.cameraStreaming = true;
        return 0;
    }

    if (request == VIDIOC_STREAMOFF)
    {
        slot.cameraStreaming = false;
        return 0;
    }

    if (request == VIDIOC_DQBUF)
    {
        auto *buffer = static_cast<V4l2Buffer *>(argument);
        if (!slot.cameraStreaming)
        {
            errno = EIO;
            return -1;
        }
        buffer->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer->memory = V4L2_MEMORY_MMAP;
        buffer->index = slot.cameraNextBuffer++ % 2;
        buffer->bytesUsed = slot.cameraImageSize;
        buffer->length = slot.cameraImageSize;
        buffer->offset = buffer->index * slot.cameraImageSize;
        return 0;
    }

    errno = ENOTTY;
    return -1;
}

void *mapDevice(int fd, void *address, size_t length, int protection,
                int flags, off_t offset)
{
    (void)address;
    (void)protection;
    (void)flags;
    std::lock_guard<std::mutex> lock(slotsMutex);
    Slot *slot = findSlot(fd);
    if (!slot || slot->kind != Kind::Camera)
        return nullptr;

    const size_t index = static_cast<size_t>(offset / slot->cameraImageSize);
    if (index >= 2 || length < slot->cameraImageSize)
    {
        errno = EINVAL;
        return nullptr;
    }
    if (slot->cameraMappings.size() <= index)
    {
        slot->cameraMappings.resize(index + 1, nullptr);
        slot->cameraMappingLengths.resize(index + 1, 0);
    }
    if (!slot->cameraMappings[index])
    {
        slot->cameraMappings[index] = VirtualAlloc(nullptr, length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        slot->cameraMappingLengths[index] = length;
        fillCameraFrame(slot->cameraMappings[index], length, slot->cameraWidth, slot->cameraHeight);
    }
    return slot->cameraMappings[index];
}

int ioctlDevice(int fd, unsigned long request, void *argument)
{
    std::lock_guard<std::mutex> lock(slotsMutex);
    Slot *slot = findSlot(fd);
    if (!slot)
    {
        errno = EBADF;
        return -1;
    }

    /* Linux joystick ABI queries used by the shared input probe. */
    if (slot->kind == Kind::Joystick && argument)
    {
        if (request == 0x80016a11) // JSIOCGAXES
            *static_cast<unsigned char *>(argument) = 4;
        else if (request == 0x80016a12) // JSIOCGBUTTONS
            *static_cast<unsigned char *>(argument) = 16;
        else if ((request & 0xffff) == 0x6a13) // JSIOCGNAME(len)
            std::strncpy(static_cast<char *>(argument), "Pacloader ES1 input", 64);
    }
    else if (slot->kind == Kind::Camera)
    {
        return cameraIoctl(*slot, request, argument);
    }
    return 0;
}

const VirtualDeviceRegistry::Device serialDevice{
    "namco-es1-serial-input",
    claims,
    openDevice,
    owns,
    available,
    readDevice,
    writeDevice,
    closeDevice,
    ioctlDevice,
    mapDevice};
}

extern "C" void es1RegisterVirtualDevices(void)
{
    const VirtualDeviceRegistry::Device steeringDevice{
        "System ES1 steering PCB (/dev/ttyS1)",
        es1KickbackClaimsPath,
        es1KickbackOpen,
        es1KickbackOwnsDescriptor,
        es1KickbackBytesAvailable,
        es1KickbackRead,
        es1KickbackWrite,
        es1KickbackClose,
        es1KickbackIoctl,
        nullptr};
    VirtualDeviceRegistry::registerDevice(steeringDevice);
    const VirtualDeviceRegistry::Device jvsDevice{
        "System ES1 JAMMA/JVS (/dev/ttyS2)",
        [](const char *path) {
            return es1JvsSerialEnabled() && path && std::strcmp(path, "/dev/ttyS2") == 0;
        },
        es1JvsSerialOpen,
        es1JvsSerialIsDescriptor,
        es1JvsSerialBytesAvailable,
        es1JvsSerialRead,
        es1JvsSerialWrite,
        es1JvsSerialClose,
        es1JvsSerialIoctl,
        nullptr};
    VirtualDeviceRegistry::registerDevice(jvsDevice);
    VirtualDeviceRegistry::registerDevice(serialDevice);
}
