// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/netlink_socket.h"

#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <sys/socket.h>

#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

#include "shill/net/netlink_message.h"

// This is from a version of linux/socket.h that we don't have.
#define SOL_NETLINK 270

namespace shill {

std::unique_ptr<NetlinkSocket> NetlinkSocket::Create() {
  return CreateWithSocketFactory(std::make_unique<net_base::SocketFactory>());
}

std::unique_ptr<NetlinkSocket> NetlinkSocket::CreateWithSocketFactory(
    std::unique_ptr<net_base::SocketFactory> socket_factory) {
  std::unique_ptr<net_base::Socket> socket =
      socket_factory->CreateNetlink(NETLINK_GENERIC, 0);
  if (socket == nullptr) {
    PLOG(ERROR) << "Failed to create AF_NETLINK socket";
    return nullptr;
  }

  return std::unique_ptr<NetlinkSocket>(new NetlinkSocket(std::move(socket)));
}

NetlinkSocket::NetlinkSocket(std::unique_ptr<net_base::Socket> socket)
    : socket_(std::move(socket)) {}
NetlinkSocket::~NetlinkSocket() = default;

bool NetlinkSocket::RecvMessage(std::vector<uint8_t>* message) {
  if (!message) {
    LOG(ERROR) << "Null |message|";
    return false;
  }

  // Determine the amount of data currently waiting.
  const size_t kFakeReadByteCount = 1;
  std::vector<uint8_t> fake_read(kFakeReadByteCount);
  const std::optional<size_t> result =
      socket_->RecvFrom(fake_read, MSG_TRUNC | MSG_PEEK, nullptr, nullptr);
  if (!result.has_value()) {
    PLOG(ERROR) << "Socket recvfrom failed.";
    return false;
  }

  // Read the data that was waiting when we did our previous read.
  message->resize(*result, 0);
  if (!socket_->RecvFrom(*message, 0, nullptr, nullptr).has_value()) {
    PLOG(ERROR) << "Second socket recvfrom failed.";
    return false;
  }
  return true;
}

bool NetlinkSocket::SendMessage(base::span<const uint8_t> out_msg) {
  const std::optional<size_t> result = socket_->Send(out_msg, 0);
  if (!result) {
    PLOG(ERROR) << "Send failed.";
    return false;
  }
  if (*result != out_msg.size()) {
    LOG(ERROR) << "Only sent " << *result << " bytes out of " << out_msg.size()
               << ".";
    return false;
  }

  return true;
}

bool NetlinkSocket::SubscribeToEvents(uint32_t group_id) {
  int err = setsockopt(socket_->Get(), SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                       &group_id, sizeof(group_id));
  if (err < 0) {
    PLOG(ERROR) << "setsockopt didn't work.";
    return false;
  }
  return true;
}

int NetlinkSocket::WaitForRead(struct timeval* timeout) const {
  fd_set read_fds;
  FD_ZERO(&read_fds);
  if (socket_->Get() >= FD_SETSIZE) {
    LOG(ERROR) << "Invalid file_descriptor: " << socket_->Get();
    return -1;
  }
  FD_SET(socket_->Get(), &read_fds);

  return HANDLE_EINTR(
      select(socket_->Get() + 1, &read_fds, nullptr, nullptr, timeout));
}

uint32_t NetlinkSocket::GetSequenceNumber() {
  if (++sequence_number_ == NetlinkMessage::kBroadcastSequenceNumber) {
    ++sequence_number_;
  }
  return sequence_number_;
}

}  // namespace shill.
