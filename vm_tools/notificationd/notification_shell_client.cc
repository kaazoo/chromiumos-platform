// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/notificationd/notification_shell_client.h"

#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_util.h>
#include <base/threading/thread_task_runner_handle.h>
#include <linux/virtwl.h>
#include <wayland-client.h>
#include <wayland-util.h>

namespace {

// Callback for checking whether it's called. Used in
// NotificationShellClient::WaitForSync().
void SyncCallback(void* data, wl_callback* callback, uint32_t serial) {
  *static_cast<bool*>(data) = true;
}
constexpr wl_callback_listener kSyncListener = {SyncCallback};

// Wraps wl_callback in a std::unique_ptr.
struct WlCallbackDeleter {
  void operator()(wl_callback* callback) { wl_callback_destroy(callback); }
};
using WlCallback = std::unique_ptr<wl_callback, WlCallbackDeleter>;

// Buffer size of the message used for ioctl to virtwl.
constexpr size_t kBufferSize = 4096;

}  // namespace

namespace vm_tools {
namespace notificationd {

NotificationShellClient::NotificationClient::NotificationClient(
    zcr_notification_shell_notification_v1* proxy,
    const std::string& notification_key,
    NotificationShellClient* shell_client)
    : proxy_(proxy),
      notification_key_(notification_key),
      shell_client_(shell_client) {
  // zcr_notification_shell_notification_v1_add_listener method is automatically
  // generated by wayland-scanner according to the
  // vm_tools/notificationd/protocol/notification-shell-unstable-v1.xml
  zcr_notification_shell_notification_v1_add_listener(
      proxy_.get(), &notification_listener_, this);
}

void NotificationShellClient::NotificationClient::Close() {
  DCHECK(proxy_);
  // zcr_notification_shell_notification_v1_close method is automatically
  // generated by wayland-scanner according to the
  // vm_tools/notificationd/protocol/notification-shell-unstable-v1.xml
  zcr_notification_shell_notification_v1_close(proxy_.get());
}

void NotificationShellClient::NotificationClient::HandleNotificationClosedEvent(
    bool by_user) {
  shell_client_->HandleNotificationClosedEvent(notification_key_, by_user);
}

void NotificationShellClient::NotificationClient::
    HandleNotificationClickedEvent(int32_t button_index) {
  shell_client_->HandleNotificationClickedEvent(notification_key_,
                                                button_index);
}

void NotificationShellClient::NotificationClient::
    HandleNotificationClosedEventCallback(
        void* data,
        zcr_notification_shell_notification_v1* notification_proxy,
        uint32_t by_user) {
  static_cast<
      vm_tools::notificationd::NotificationShellClient::NotificationClient*>(
      data)
      ->HandleNotificationClosedEvent(by_user);
}

void NotificationShellClient::NotificationClient::
    HandleNotificationClickedEventCallback(
        void* data,
        zcr_notification_shell_notification_v1* notification_proxy,
        int32_t button_index) {
  static_cast<
      vm_tools::notificationd::NotificationShellClient::NotificationClient*>(
      data)
      ->HandleNotificationClickedEvent(button_index);
}

NotificationShellClient::NotificationShellClient(
    NotificationShellInterface* interface, base::OnceClosure quit_closure)
    : interface_(interface), quit_closure_(std::move(quit_closure)) {}

void NotificationShellClient::OnEventReadable() {
  if (wl_event_loop_dispatch(event_loop_.get(), 0) < 0) {
    PLOG(ERROR) << "Failed to dispatch event loop for wayland";
    if (quit_closure_) {
      base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                    std::move(quit_closure_));
    }
  }
}

// static
std::unique_ptr<NotificationShellClient> NotificationShellClient::Create(
    const std::string& display_name,
    const std::string& virtwl_device,
    NotificationShellInterface* interface,
    base::OnceClosure quit_closure) {
  auto client = base::WrapUnique(
      new NotificationShellClient(interface, std::move(quit_closure)));

  if (!client->Init(display_name.empty() ? nullptr : display_name.c_str(),
                    virtwl_device.empty() ? nullptr : virtwl_device.c_str()))
    return nullptr;

  return client;
}

bool NotificationShellClient::Init(const char* display_name,
                                   const char* virtwl_device) {
  event_loop_.reset(wl_event_loop_create());
  event_loop_fd_.reset(wl_event_loop_get_fd(event_loop_.get()));
  if (!event_loop_fd_.is_valid()) {
    PLOG(ERROR) << "Could not get wayland event loop fd";
    return false;
  }

  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      event_loop_fd_.get(),
      base::BindRepeating(&NotificationShellClient::OnEventReadable,
                          base::Unretained(this)));
  if (!watcher_) {
    LOG(ERROR) << "Failed to watch event loop fd";
    return false;
  }

  if (virtwl_device) {
    const base::ScopedFD virtwl_fd(open(virtwl_device, O_RDWR | O_CLOEXEC));
    if (!virtwl_fd.is_valid()) {
      PLOG(ERROR) << "Could not open " << virtwl_device;
      return false;
    }

    int fds[2] = {};
    // Connection to virtwl channel.
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds)) {
      PLOG(ERROR) << "Failed to create socket pair";
      return false;
    }
    virtwl_socket_fd_.reset(fds[0]);
    const auto virtwl_display_fd = fds[1];

    virtwl_ioctl_new new_ctx = {
        .type = VIRTWL_IOCTL_NEW_CTX,
        .fd = -1,
        .flags = 0,
        .size = 0,
    };
    if (ioctl(virtwl_fd.get(), VIRTWL_IOCTL_NEW, &new_ctx)) {
      PLOG(ERROR) << "Failed to create virtwl context";
      return false;
    }
    virtwl_ctx_fd_.reset(new_ctx.fd);

    wl_event_loop_add_fd(event_loop_.get(), virtwl_socket_fd_.get(),
                         WL_EVENT_READABLE, HandleVirtwlSocketEventCallback,
                         this);
    wl_event_loop_add_fd(event_loop_.get(), virtwl_ctx_fd_.get(),
                         WL_EVENT_READABLE, HandleVirtwlCtxEventCallback, this);

    // The |display_| takes ownership of |virtwl_display_fd| and will close it
    // when |display_| is destroyed.
    display_.reset(wl_display_connect_to_fd(virtwl_display_fd));
  } else {
    display_.reset(wl_display_connect(display_name));
  }

  if (!display_) {
    LOG(ERROR) << "Failed to connect to the display";
    return false;
  }

  wl_event_loop_add_fd(event_loop_.get(), wl_display_get_fd(display_.get()),
                       WL_EVENT_READABLE, HandleEventCallback, this);

  wl_registry_add_listener(wl_display_get_registry(display_.get()),
                           &registry_listener_, this);

  // We use WaitForSync method instead of wl_display_roundtrip because we have
  // to handle message forwarding to/from virtwl in single-thread when virtwl is
  // used, which can be invoked by observing |event_loop_|. Calling
  // wl_display_roundtrip, which does not handle |event_loop_|, causes deadlock
  // because HandleVirtwlCtxEvent and HandleVirtwlSocketEvent are never called.
  WaitForSync();

  if (!proxy_) {
    LOG(ERROR) << "Server is missing the zcr_notification_shell_v1 interface";
    return false;
  }

  return true;
}

void NotificationShellClient::WaitForSync() {
  const WlCallback callback(wl_display_sync(display_.get()));
  DCHECK(callback);

  bool done = false;
  wl_callback_add_listener(callback.get(), &::kSyncListener, &done);
  wl_display_flush(display_.get());

  while (!done)
    wl_event_loop_dispatch(event_loop_.get(), -1 /*no timeout*/);
}

bool NotificationShellClient::CreateNotification(
    const std::string& title,
    const std::string& message,
    const std::string& display_source,
    const std::string& notification_key,
    const std::vector<std::string>& buttons) {
  DCHECK(proxy_);

  // Convert vector of strings to wl_array
  wl_array buttons_wl_array;
  wl_array_init(&buttons_wl_array);
  for (const auto& button : buttons) {
    const auto size = button.length() + 1;
    base::strlcpy(static_cast<char*>(wl_array_add(&buttons_wl_array, size)),
                  button.c_str(), size);
  }

  // zcr_notification_shell_v1_create_notification method is automatically
  // generated by wayland-scanner according to the
  // vm_tools/notificationd/protocol/notification-shell-unstable-v1.xml
  auto* notification_proxy = zcr_notification_shell_v1_create_notification(
      proxy_.get(), title.c_str(), message.c_str(), display_source.c_str(),
      notification_key.c_str(), &buttons_wl_array);

  wl_array_release(&buttons_wl_array);

  // The notification client takes ownership of |notification_proxy|
  notification_clients_[notification_key] =
      std::make_unique<NotificationClient>(notification_proxy, notification_key,
                                           this);

  wl_display_flush(display_.get());
  return true;
}

bool NotificationShellClient::CloseNotification(
    const std::string& notification_key) {
  DCHECK(proxy_);

  auto notification = notification_clients_.find(notification_key);
  if (notification == notification_clients_.end()) {
    LOG(ERROR) << "Invalid notification key";
    return false;
  }
  notification->second->Close();

  wl_display_flush(display_.get());
  return true;
}

void NotificationShellClient::HandleNotificationClosedEvent(
    const std::string& notification_key, bool by_user) {
  interface_->OnClosed(notification_key, by_user);

  auto notification = notification_clients_.find(notification_key);
  DCHECK(notification != notification_clients_.end());
  notification_clients_.erase(notification);
}

void NotificationShellClient::HandleNotificationClickedEvent(
    const std::string& notification_key, int32_t button_index) {
  interface_->OnClicked(notification_key, button_index);
}

void NotificationShellClient::HandleRegistry(wl_registry* registry,
                                             int32_t id,
                                             const char* interface,
                                             uint32_t version) {
  if (std::string(interface) == "zcr_notification_shell_v1") {
    proxy_.reset(static_cast<zcr_notification_shell_v1*>(wl_registry_bind(
        registry, id, &zcr_notification_shell_v1_interface, 1)));
  }
}

int NotificationShellClient::HandleEvent(uint32_t mask) {
  if (mask & WL_EVENT_HANGUP) {
    LOG(ERROR) << "Wayland connection hung up";
    if (quit_closure_) {
      base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                    std::move(quit_closure_));
    }
    return -1;
  }
  if (mask & WL_EVENT_ERROR) {
    LOG(ERROR) << "Wayland connection error occurred";
    if (quit_closure_) {
      base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                    std::move(quit_closure_));
    }
    return -1;
  }

  int count = 0;
  if (mask & WL_EVENT_READABLE)
    count = wl_display_dispatch(display_.get());

  if (mask == 0) {
    count = wl_display_dispatch_pending(display_.get());
    wl_display_flush(display_.get());
  }

  return count;
}

void NotificationShellClient::HandleVirtwlCtxEvent() {
  // virtwl_ioctl_txn::data, which is the last element of the structure, is
  // defined as zero-length array. So, we allocate the space for that by casting
  // a buffer array (|ioctl_buffer|) into the structure (|ioctl_recv|).
  uint8_t ioctl_buffer[kBufferSize] = {};
  virtwl_ioctl_txn* ioctl_recv =
      reinterpret_cast<virtwl_ioctl_txn*>(ioctl_buffer);

  // virtwl_ioctl_txn::len is the reserved size of the data element
  // (virtwl_ioctl_txn::data). Because the data element is zero-length array,
  // the size can be calculated by subtracting the header size from the total
  // buffer size used for initializing the structure.
  ioctl_recv->len = sizeof(ioctl_buffer) - sizeof(virtwl_ioctl_txn);

  if (ioctl(virtwl_ctx_fd_.get(), VIRTWL_IOCTL_RECV, ioctl_recv)) {
    LOG(ERROR) << "Failed to receive data from virtwl context";
    if (quit_closure_) {
      base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                    std::move(quit_closure_));
    }
  }

  iovec buffer_iov = {.iov_base = &ioctl_recv->data,
                      .iov_len = ioctl_recv->len};

  msghdr msg = {.msg_iov = &buffer_iov, .msg_iovlen = 1, .msg_controllen = 0};

  const auto bytes = sendmsg(virtwl_socket_fd_.get(), &msg, MSG_NOSIGNAL);
  DCHECK_EQ(bytes, ioctl_recv->len);

  // Count how many FDs the kernel gave us. We do not forwards FDs in
  // notificationd. If any FDs are included in ioctl_recv, we just ignore them.
  // We can do this because noification shell protocol does not use FDs.
  size_t fd_count = 0;
  for (; fd_count < VIRTWL_SEND_MAX_ALLOCS; ++fd_count) {
    if (ioctl_recv->fds[fd_count] < 0)
      break;
  }

  DCHECK_EQ(fd_count, 0);
}

void NotificationShellClient::HandleVirtwlSocketEvent() {
  // virtwl_ioctl_txn::data, which is the last element of the structure, is
  // defined as zero-length array. So, we allocate the space for that by casting
  // a buffer array (|ioctl_buffer|) into the structure (|ioctl_recv|).
  uint8_t ioctl_buffer[kBufferSize] = {};
  virtwl_ioctl_txn* ioctl_send =
      reinterpret_cast<virtwl_ioctl_txn*>(ioctl_buffer);

  for (int i = 0; i < VIRTWL_SEND_MAX_ALLOCS; ++i)
    ioctl_send->fds[i] = -1;

  // iovec::iov_len is the reserved size of iovec::iov_base. Because the data
  // element in virtwl_ioctl_txn is defined as zero-length array, its size can
  // be calculated by subtracting the header size from the total buffer size
  // used for initializing the structure.
  iovec buffer_iov = {
      .iov_base = &ioctl_send->data,
      .iov_len = sizeof(ioctl_buffer) - sizeof(virtwl_ioctl_txn)};

  uint8_t fd_buffer[CMSG_LEN(sizeof(int) * VIRTWL_SEND_MAX_ALLOCS)] = {};
  msghdr msg = {.msg_iov = &buffer_iov,
                .msg_iovlen = 1,
                .msg_control = fd_buffer,
                .msg_controllen = sizeof(fd_buffer)};

  const auto bytes = recvmsg(virtwl_socket_fd_.get(), &msg, 0);
  DCHECK_GT(bytes, 0);

  // The data were extracted from the recvmsg call into the ioctl_send
  // structure which we now pass along to the kernel.
  ioctl_send->len = bytes;
  auto ret = ioctl(virtwl_ctx_fd_.get(), VIRTWL_IOCTL_SEND, ioctl_send);
  DCHECK_EQ(ret, 0);

  // We do not forwards FDs in notificationd. If any FDs are included in msg,
  // we just ignore them. We can do this because noification shell protocol does
  // not use FDs.
  size_t fd_count = 0;
  for (auto* cmsg = msg.msg_controllen != 0 ? CMSG_FIRSTHDR(&msg) : nullptr;
       cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
      continue;

    const auto cmsg_fd_count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
    fd_count += cmsg_fd_count;
  }
  DCHECK_EQ(fd_count, 0);
}

int NotificationShellClient::HandleEventCallback(int fd,
                                                 uint32_t mask,
                                                 void* data) {
  return static_cast<vm_tools::notificationd::NotificationShellClient*>(data)
      ->HandleEvent(mask);
}

void NotificationShellClient::HandleRegistryCallback(void* data,
                                                     wl_registry* registry,
                                                     uint32_t id,
                                                     const char* interface,
                                                     uint32_t version) {
  static_cast<vm_tools::notificationd::NotificationShellClient*>(data)
      ->HandleRegistry(registry, id, interface, version);
}

int NotificationShellClient::HandleVirtwlSocketEventCallback(int fd,
                                                             uint32_t mask,
                                                             void* data) {
  static_cast<vm_tools::notificationd::NotificationShellClient*>(data)
      ->HandleVirtwlSocketEvent();
  return 1;
}

int NotificationShellClient::HandleVirtwlCtxEventCallback(int fd,
                                                          uint32_t mask,
                                                          void* data) {
  static_cast<vm_tools::notificationd::NotificationShellClient*>(data)
      ->HandleVirtwlCtxEvent();
  return 1;
}

}  // namespace notificationd
}  // namespace vm_tools
