/**
 * @file src/platform/esp32/serial_port.cpp
 * @brief POSIX and Win32 serial port implementations for the ESP32-S3 bridge.
 */

// standard includes
#include <algorithm>
#include <utility>

// local includes
#include "platform/esp32/esp32_bridge_protocol.h"
#include "platform/esp32/serial_port.hpp"

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif

  // platform includes
  #include <windows.h>
  // initguid must precede devguid so GUID_DEVCLASS_PORTS is defined here.
  #include <cfgmgr32.h>
  #include <devguid.h>
  #include <initguid.h>
  #include <setupapi.h>
#else
  // standard includes
  #include <cerrno>
  #include <filesystem>
  #include <fstream>

  // platform includes
  #include <fcntl.h>
  #include <poll.h>
  #include <sys/ioctl.h>
  #include <termios.h>
  #include <unistd.h>
#endif

namespace lvh::detail::esp32 {
  namespace {

#if defined(_WIN32)
    /**
     * @brief Win32 COM port using overlapped I/O.
     *
     * A synchronous handle would serialize the reader's blocking ReadFile with
     * input-report writes and add up to one read timeout of input latency.
     */
    class Win32SerialPort final: public SerialPort {
    public:
      Win32SerialPort(HANDLE handle, std::string path):
          handle_ {handle},
          path_ {std::move(path)},
          read_event_ {CreateEventA(nullptr, TRUE, FALSE, nullptr)},
          write_event_ {CreateEventA(nullptr, TRUE, FALSE, nullptr)} {}

      ~Win32SerialPort() override {
        CancelIoEx(handle_, nullptr);
        CloseHandle(handle_);
        CloseHandle(read_event_);
        CloseHandle(write_event_);
      }

      bool write(std::span<const std::uint8_t> data) override {
        while (!data.empty()) {
          OVERLAPPED overlapped {};
          overlapped.hEvent = write_event_;
          DWORD written = 0;
          if (!WriteFile(handle_, data.data(), static_cast<DWORD>(data.size()), &written, &overlapped)) {
            if (GetLastError() != ERROR_IO_PENDING || !GetOverlappedResult(handle_, &overlapped, &written, TRUE)) {
              return false;
            }
          }
          if (written == 0) {
            return false;
          }
          data = data.subspan(written);
        }
        return true;
      }

      std::ptrdiff_t read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) override {
        const auto timeout_ms = static_cast<DWORD>(std::max<std::chrono::milliseconds::rep>(timeout.count(), 1));
        if (timeout_ms != configured_timeout_ms_) {
          // MAXDWORD interval and multiplier: complete as soon as any byte
          // arrives, otherwise after the constant timeout.
          COMMTIMEOUTS timeouts {};
          timeouts.ReadIntervalTimeout = MAXDWORD;
          timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
          timeouts.ReadTotalTimeoutConstant = timeout_ms;
          timeouts.WriteTotalTimeoutConstant = 1000;
          if (!SetCommTimeouts(handle_, &timeouts)) {
            return -1;
          }
          configured_timeout_ms_ = timeout_ms;
        }
        OVERLAPPED overlapped {};
        overlapped.hEvent = read_event_;
        DWORD read = 0;
        if (!ReadFile(handle_, buffer.data(), static_cast<DWORD>(buffer.size()), &read, &overlapped)) {
          if (GetLastError() != ERROR_IO_PENDING || !GetOverlappedResult(handle_, &overlapped, &read, TRUE)) {
            return -1;
          }
        }
        return static_cast<std::ptrdiff_t>(read);
      }

      std::string path() const override {
        return path_;
      }

      /**
       * @brief Check that the overlapped I/O events were created.
       *
       * @return `true` when the port can be used.
       */
      bool valid() const {
        return read_event_ != nullptr && write_event_ != nullptr;
      }

    private:
      HANDLE handle_;
      std::string path_;
      HANDLE read_event_;
      HANDLE write_event_;
      DWORD configured_timeout_ms_ = 0;
    };

    std::string read_port_name(HDEVINFO devices, SP_DEVINFO_DATA &device) {
      HKEY key = SetupDiOpenDevRegKey(devices, &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
      if (key == INVALID_HANDLE_VALUE) {
        return {};
      }
      char name[64] {};
      DWORD size = sizeof(name) - 1U;
      DWORD type = 0;
      const auto status = RegQueryValueExA(key, "PortName", nullptr, &type, reinterpret_cast<LPBYTE>(name), &size);
      RegCloseKey(key);
      return status == ERROR_SUCCESS && type == REG_SZ ? std::string {name} : std::string {};
    }

    bool parent_has_bridge_serial(DEVINST instance) {
      DEVINST parent = 0;
      if (CM_Get_Parent(&parent, instance, 0) != CR_SUCCESS) {
        return false;
      }
      char id[MAX_DEVICE_ID_LEN] {};
      if (CM_Get_Device_IDA(parent, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
        return false;
      }
      // Composite parent instance ids end in the USB serial number:
      // USB\VID_303A&PID_40F0\LVHB-0123456789AB
      const std::string parent_id {id};
      const auto separator = parent_id.rfind('\\');
      return separator != std::string::npos && parent_id.compare(separator + 1U, std::char_traits<char>::length(LVHB_SERIAL_PREFIX), LVHB_SERIAL_PREFIX) == 0;
    }
#else
    /**
     * @brief POSIX tty.
     */
    class PosixSerialPort final: public SerialPort {
    public:
      PosixSerialPort(int fd, std::string path):
          fd_ {fd},
          path_ {std::move(path)} {}

      ~PosixSerialPort() override {
        ::close(fd_);
      }

      bool write(std::span<const std::uint8_t> data) override {
        while (!data.empty()) {
          const auto written = ::write(fd_, data.data(), data.size());
          if (written > 0) {
            data = data.subspan(static_cast<std::size_t>(written));
            continue;
          }
          if (written < 0 && (errno == EAGAIN || errno == EINTR)) {
            pollfd descriptor {fd_, POLLOUT, 0};
            if (::poll(&descriptor, 1, 1000) <= 0) {
              return false;
            }
            continue;
          }
          return false;
        }
        return true;
      }

      std::ptrdiff_t read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) override {
        pollfd descriptor {fd_, POLLIN, 0};
        const auto ready = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
        if (ready == 0) {
          return 0;
        }
        if (ready < 0) {
          return errno == EINTR ? 0 : -1;
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && (descriptor.revents & POLLIN) == 0) {
          return -1;
        }
        const auto count = ::read(fd_, buffer.data(), buffer.size());
        if (count < 0) {
          return errno == EAGAIN || errno == EINTR ? 0 : -1;
        }
        // A readable descriptor that returns no data means the device vanished.
        return count == 0 ? -1 : count;
      }

      std::string path() const override {
        return path_;
      }

    private:
      int fd_;
      std::string path_;
    };
#endif

  }  // namespace

  std::unique_ptr<SerialPort> open_serial_port(const std::string &path) {
#if defined(_WIN32)
    const auto device_path = path.starts_with("\\\\.\\") ? path : "\\\\.\\" + path;
    HANDLE handle = CreateFileA(device_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      return nullptr;
    }
    DCB dcb {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) {
      CloseHandle(handle);
      return nullptr;
    }
    // The baud rate is ignored by CDC-ACM but must be valid.
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    // The firmware only talks while DTR is asserted.
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(handle, &dcb)) {
      CloseHandle(handle);
      return nullptr;
    }
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    auto port = std::make_unique<Win32SerialPort>(handle, path);
    if (!port->valid()) {
      return nullptr;
    }
    return port;
#else
    const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
      return nullptr;
    }
    // Keep other unprivileged processes from opening the port while we own it (Windows COM ports are exclusive already).
    if (::ioctl(fd, TIOCEXCL) != 0) {
      ::close(fd);
      return nullptr;
    }
    termios options {};
    if (::tcgetattr(fd, &options) != 0) {
      ::close(fd);
      return nullptr;
    }
    ::cfmakeraw(&options);
    options.c_cflag |= CLOCAL | CREAD;
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 0;
    ::cfsetispeed(&options, B115200);
    ::cfsetospeed(&options, B115200);
    if (::tcsetattr(fd, TCSANOW, &options) != 0) {
      ::close(fd);
      return nullptr;
    }
    ::tcflush(fd, TCIOFLUSH);
    return std::make_unique<PosixSerialPort>(fd, path);
#endif
  }

  std::vector<std::string> find_bridge_ports() {
    std::vector<std::string> ports;
#if defined(_WIN32)
    HDEVINFO devices = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
      return ports;
    }
    SP_DEVINFO_DATA device {};
    device.cbSize = sizeof(device);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(devices, index, &device); ++index) {
      if (!parent_has_bridge_serial(device.DevInst)) {
        continue;
      }
      if (auto name = read_port_name(devices, device); !name.empty()) {
        ports.push_back(std::move(name));
      }
    }
    SetupDiDestroyDeviceInfoList(devices);
#elif defined(__linux__)
    namespace fs = std::filesystem;
    std::error_code error;
    for (const auto &entry : fs::directory_iterator {"/sys/class/tty", error}) {
      const auto name = entry.path().filename().string();
      if (!name.starts_with("ttyACM")) {
        continue;
      }
      // .../usbX/X-Y/X-Y:1.0/tty/ttyACMn -> the USB device directory holds `serial`.
      const auto interface_path = fs::canonical(entry.path() / "device", error);
      if (error) {
        error.clear();
        continue;
      }
      std::ifstream serial_file {interface_path.parent_path() / "serial"};
      std::string serial;
      std::getline(serial_file, serial);
      if (serial.starts_with(LVHB_SERIAL_PREFIX)) {
        ports.push_back("/dev/" + name);
      }
    }
    std::ranges::sort(ports);
#endif
    return ports;
  }

  SerialPortOpener make_serial_port_opener(std::string configured_path) {
    return [configured_path = std::move(configured_path)]() -> std::unique_ptr<SerialPort> {
      if (!configured_path.empty()) {
        return open_serial_port(configured_path);
      }
      for (const auto &path : find_bridge_ports()) {
        if (auto port = open_serial_port(path); port != nullptr) {
          return port;
        }
      }
      return nullptr;
    };
  }

}  // namespace lvh::detail::esp32
