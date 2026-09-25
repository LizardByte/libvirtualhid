// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

#pragma once

#include "protocol.hpp"

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace lvh::detail::macos_broker {

  inline bool transfer(int fd, void *buffer, std::size_t size, bool sending) {
    auto *bytes = static_cast<std::uint8_t *>(buffer);
    while (size != 0) {
      const auto count = sending ?
                           ::send(fd, bytes, size, 0) :
                           ::recv(fd, bytes, size, 0);
      if (count < 0 && errno == EINTR) {
        continue;
      }
      if (count <= 0) {
        return false;
      }
      bytes += count;
      size -= static_cast<std::size_t>(count);
    }
    return true;
  }

  inline bool send_message(int fd, const Message &message) {
    return transfer(fd, const_cast<Message *>(&message), sizeof(message), true);
  }

  inline bool receive_message(int fd, Message &message) {
    return transfer(fd, &message, sizeof(message), false) && message.version == protocol_version;
  }

  inline int connect_to_broker(std::string &error) {
    struct stat directory_stat {};
    if (::lstat("/var/run/libvirtualhid", &directory_stat) != 0 ||
        !S_ISDIR(directory_stat.st_mode) || directory_stat.st_uid != 0 ||
        (directory_stat.st_mode & 0022) != 0) {
      error = "macOS broker directory is missing or insecure";
      return -1;
    }

    struct stat socket_stat {};
    if (::lstat(socket_path, &socket_stat) != 0 || !S_ISSOCK(socket_stat.st_mode) || socket_stat.st_uid != 0) {
      error = "installed macOS broker socket is missing or untrusted";
      return -1;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
      error = std::strerror(errno);
      return -1;
    }
    const int no_sigpipe = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) != 0) {
      error = std::strerror(errno);
      ::close(fd);
      return -1;
    }
    timeval send_timeout {.tv_sec = 5, .tv_usec = 0};
    timeval receive_timeout {.tv_sec = 30, .tv_usec = 0};
    static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout)));
    static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout)));
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1U);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
      error = std::strerror(errno);
      ::close(fd);
      return -1;
    }

    uid_t peer_uid = static_cast<uid_t>(-1);
    gid_t peer_gid = static_cast<gid_t>(-1);
    if (::getpeereid(fd, &peer_uid, &peer_gid) != 0 || peer_uid != 0) {
      error = "macOS broker peer is not root";
      ::close(fd);
      return -1;
    }
    return fd;
  }

}  // namespace lvh::detail::macos_broker
