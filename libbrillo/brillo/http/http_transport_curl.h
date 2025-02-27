// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_HTTP_HTTP_TRANSPORT_CURL_H_
#define LIBBRILLO_BRILLO_HTTP_HTTP_TRANSPORT_CURL_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/location.h>
#include <base/memory/weak_ptr.h>
#include <brillo/brillo_export.h>
#include <brillo/http/curl_api.h>
#include <brillo/http/http_transport.h>

namespace brillo {
namespace http {
namespace curl {

class Connection;

///////////////////////////////////////////////////////////////////////////////
// An implementation of http::Transport that uses libcurl for
// HTTP communications. This class (as http::Transport base)
// is used by http::Request and http::Response classes to provide HTTP
// functionality to the clients.
// See http_transport.h for more details.
///////////////////////////////////////////////////////////////////////////////
class BRILLO_EXPORT Transport : public http::Transport {
 public:
  // Constructs the transport using the current message loop for async
  // operations.
  explicit Transport(const std::shared_ptr<CurlInterface>& curl_interface);
  // Creates a transport object using a proxy.
  // |proxy| is of the form [protocol://][user:password@]host[:port].
  // If not defined, protocol is assumed to be http://.
  Transport(const std::shared_ptr<CurlInterface>& curl_interface,
            const std::string& proxy);
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  ~Transport() override;

  // Overrides from http::Transport.
  std::shared_ptr<http::Connection> CreateConnection(
      const std::string& url,
      const std::string& method,
      const HeaderList& headers,
      const std::string& user_agent,
      const std::string& referer,
      brillo::ErrorPtr* error) override;

  void RunCallbackAsync(const base::Location& from_here,
                        base::OnceClosure callback) override;

  RequestID StartAsyncTransfer(http::Connection* connection,
                               SuccessCallback success_callback,
                               ErrorCallback error_callback) override;

  bool CancelRequest(RequestID request_id) override;

  void SetDefaultTimeout(base::TimeDelta timeout) override;

  void SetInterface(const std::string& ifname) override;

  void SetLocalIpAddress(const std::string& ip_address) override;

  void SetDnsServers(const std::vector<std::string>& dns_servers) override;

  void SetDnsInterface(const std::string& dns_interface) override;

  void SetDnsLocalIPv4Address(const std::string& dns_ipv4_addr) override;

  void SetDnsLocalIPv6Address(const std::string& dns_ipv6_addr) override;

  void UseDefaultCertificate() override;

  void UseCustomCertificate(Certificate cert) override;

  void ResolveHostToIp(const std::string& host,
                       uint16_t port,
                       const std::string& ip_address) override;

  void SetBufferSize(std::optional<int> buffer_size) override;
  void SetUploadBufferSize(std::optional<int> buffer_size) override;

  void SetSockOptCallback(base::RepeatingCallback<bool(int)> cb) override;

  // Helper methods to convert CURL error codes (CURLcode and CURLMcode)
  // into brillo::Error object.
  static void AddEasyCurlError(brillo::ErrorPtr* error,
                               const base::Location& location,
                               CURLcode code,
                               CurlInterface* curl_interface);

  static void AddMultiCurlError(brillo::ErrorPtr* error,
                                const base::Location& location,
                                CURLMcode code,
                                CurlInterface* curl_interface);

 protected:
  void ClearHost() override;

 private:
  // Forward-declaration of internal implementation structures.
  struct AsyncRequestData;
  class SocketPollData;

  // Initializes CURL for async operation.
  bool SetupAsyncCurl(brillo::ErrorPtr* error);

  // Stops CURL's async operations.
  void ShutDownAsyncCurl();

  // Handles all pending async messages from CURL.
  void ProcessAsyncCurlMessages();

  // Processes the transfer completion message (success or failure).
  void OnTransferComplete(http::curl::Connection* connection, CURLcode code);

  // Cleans up internal data for a completed/canceled asynchronous operation
  // on a connection.
  void CleanAsyncConnection(http::curl::Connection* connection);

  // Called after a timeout delay requested by CURL has elapsed.
  void OnTimer();

  // Callback for CURL to handle curl_socket_callback() notifications.
  // The parameters correspond to those of curl_socket_callback().
  static int MultiSocketCallback(
      CURL* easy, curl_socket_t s, int what, void* userp, void* socketp);

  // Callback for CURL to handle curl_multi_timer_callback() notifications.
  // The parameters correspond to those of curl_multi_timer_callback().
  // CURL actually uses "long" types in callback signatures, so we must comply.
  static int MultiTimerCallback(CURLM* multi,
                                long timeout_ms,  // NOLINT(runtime/int)
                                void* userp);

  std::shared_ptr<CurlInterface> curl_interface_;
  std::string proxy_;
  // CURL "multi"-handle for processing requests on multiple connections.
  CURLM* curl_multi_handle_{nullptr};
  // A map to find a corresponding Connection* using a request ID.
  std::map<RequestID, Connection*> request_id_map_;
  // Stores the connection-specific asynchronous data (such as the success
  // and error callbacks that need to be called at the end of the async
  // operation).
  std::map<Connection*, std::unique_ptr<AsyncRequestData>> async_requests_;
  // Internal data associated with in-progress asynchronous operations.
  std::map<std::pair<CURL*, curl_socket_t>, SocketPollData*> poll_data_map_;
  // The last request ID used for asynchronous operations.
  RequestID last_request_id_{0};
  // The connection timeout for the requests made.
  base::TimeDelta connection_timeout_;
  std::string interface_;
  std::string ip_address_;
  std::vector<std::string> dns_servers_;
  std::string dns_interface_;
  std::string dns_ipv4_addr_;
  std::string dns_ipv6_addr_;
  base::FilePath certificate_path_;
  curl_slist* host_list_{nullptr};
  std::optional<int> buffer_size_;
  std::optional<int> upload_buffer_size_;
  base::RepeatingCallback<bool(int)> sockopt_cb_;

  base::WeakPtrFactory<Transport> weak_ptr_factory_for_timer_{this};
  base::WeakPtrFactory<Transport> weak_ptr_factory_{this};
};

}  // namespace curl
}  // namespace http
}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_HTTP_HTTP_TRANSPORT_CURL_H_
