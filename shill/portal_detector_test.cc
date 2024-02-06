// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/portal_detector.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/time/time.h>
#include <brillo/http/http_request.h>
#include <brillo/http/mock_connection.h>
#include <brillo/http/mock_transport.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/http_url.h>

#include "shill/http_request.h"
#include "shill/mock_event_dispatcher.h"

using testing::_;
using testing::Eq;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;
using testing::Test;

namespace shill {

namespace {
const char kInterfaceName[] = "int0";
const char kHttpUrl[] = "http://www.chromium.org";
const char kHttpsUrl[] = "https://www.google.com";
const std::vector<std::string> kFallbackHttpUrls{
    "http://www.google.com/gen_204",
    "http://play.googleapis.com/generate_204",
};
const std::vector<std::string> kFallbackHttpsUrls{
    "http://url1.com/gen204",
    "http://url2.com/gen204",
};
constexpr net_base::IPAddress kDNSServer0(net_base::IPv4Address(8, 8, 8, 8));
constexpr net_base::IPAddress kDNSServer1(net_base::IPv4Address(8, 8, 4, 4));
constexpr std::string_view kPortalSignInURL = "https://portal.com/login";

class MockHttpRequest : public HttpRequest {
 public:
  MockHttpRequest()
      : HttpRequest(nullptr,
                    kInterfaceName,
                    net_base::IPFamily::kIPv4,
                    {kDNSServer0, kDNSServer1},
                    true,
                    brillo::http::Transport::CreateDefault(),
                    nullptr) {}
  MockHttpRequest(const MockHttpRequest&) = delete;
  MockHttpRequest& operator=(const MockHttpRequest&) = delete;
  ~MockHttpRequest() override = default;

  MOCK_METHOD(void,
              Start,
              (std::string_view,
               const net_base::HttpUrl&,
               const brillo::http::HeaderList&,
               base::OnceCallback<void(HttpRequest::Result)>),
              (override));
};

}  // namespace

MATCHER(PositiveDelay, "") {
  return arg.is_positive();
}

MATCHER(ZeroDelay, "") {
  return arg.is_zero();
}

class TestablePortalDetector : public PortalDetector {
 public:
  TestablePortalDetector(EventDispatcher* dispatcher,
                         const ProbingConfiguration& probing_configuration)
      : PortalDetector(
            dispatcher, kInterfaceName, probing_configuration, "tag") {}
  TestablePortalDetector(const TestablePortalDetector&) = delete;
  TestablePortalDetector& operator=(const TestablePortalDetector&) = delete;
  ~TestablePortalDetector() override = default;

  MOCK_METHOD(std::unique_ptr<HttpRequest>,
              CreateHTTPRequest,
              (const std::string& ifname,
               net_base::IPFamily ip_family,
               const std::vector<net_base::IPAddress>& dns_list,
               bool allow_non_google_https),
              (override, const));
};

class PortalDetectorTest : public Test {
 public:
  PortalDetectorTest()
      : http_probe_transport_(std::make_shared<brillo::http::MockTransport>()),
        http_probe_connection_(std::make_shared<brillo::http::MockConnection>(
            http_probe_transport_)),
        https_probe_transport_(std::make_shared<brillo::http::MockTransport>()),
        https_probe_connection_(std::make_shared<brillo::http::MockConnection>(
            https_probe_transport_)),
        interface_name_(kInterfaceName),
        dns_servers_({kDNSServer0, kDNSServer1}),
        portal_detector_(new TestablePortalDetector(
            &dispatcher_, MakeProbingConfiguration())) {}

 protected:
  class CallbackTarget {
   public:
    MOCK_METHOD(void, ResultCallback, (const PortalDetector::Result&));
  };

  static PortalDetector::ProbingConfiguration MakeProbingConfiguration() {
    PortalDetector::ProbingConfiguration config;
    config.portal_http_url = *net_base::HttpUrl::CreateFromString(kHttpUrl);
    config.portal_https_url = *net_base::HttpUrl::CreateFromString(kHttpsUrl);
    for (const auto& url : kFallbackHttpUrls) {
      config.portal_fallback_http_urls.push_back(
          *net_base::HttpUrl::CreateFromString(url));
    }
    for (const auto& url : kFallbackHttpsUrls) {
      config.portal_fallback_https_urls.push_back(
          *net_base::HttpUrl::CreateFromString(url));
    }
    return config;
  }

  PortalDetector::Result GetPortalRedirectResult(std::string_view probe_url) {
    PortalDetector::Result r;
    r.num_attempts = 1;
    r.http_result = PortalDetector::ProbeResult::kPortalRedirect;
    r.http_status_code = 302;
    r.http_content_length = 0;
    r.https_result = PortalDetector::ProbeResult::kConnectionFailure;
    r.redirect_url = net_base::HttpUrl::CreateFromString(kPortalSignInURL);
    r.probe_url = net_base::HttpUrl::CreateFromString(probe_url);
    EXPECT_TRUE(r.IsHTTPProbeComplete());
    EXPECT_TRUE(r.IsHTTPSProbeComplete());
    EXPECT_EQ(PortalDetector::ValidationState::kPortalRedirect,
              r.GetValidationState());
    return r;
  }

  void StartPortalRequest() {
    http_request_ = new StrictMock<MockHttpRequest>();
    https_request_ = new StrictMock<MockHttpRequest>();
    // Expect that PortalDetector will create the request of the HTTP probe
    // first.
    EXPECT_CALL(*portal_detector_, CreateHTTPRequest)
        .WillOnce(Return(std::unique_ptr<MockHttpRequest>(http_request_)))
        .WillOnce(Return(std::unique_ptr<MockHttpRequest>(https_request_)));
    EXPECT_CALL(*http_request_, Start);
    EXPECT_CALL(*https_request_, Start);
    portal_detector_->Start(
        net_base::IPFamily::kIPv4, {kDNSServer0, kDNSServer1},
        base::BindOnce(&CallbackTarget::ResultCallback,
                       base::Unretained(&callback_target_)));
  }

  MockHttpRequest* http_request() { return http_request_; }
  MockHttpRequest* https_request() { return https_request_; }
  PortalDetector* portal_detector() { return portal_detector_.get(); }
  MockEventDispatcher& dispatcher() { return dispatcher_; }
  CallbackTarget& callback_target() { return callback_target_; }
  brillo::http::MockConnection* http_connection() {
    return http_probe_connection_.get();
  }
  brillo::http::MockConnection* https_connection() {
    return https_probe_connection_.get();
  }

  void ExpectReset() {
    EXPECT_EQ(0, portal_detector_->attempt_count());
    ExpectCleanupTrial();
  }

  void ExpectCleanupTrial() {
    EXPECT_FALSE(portal_detector_->IsRunning());
    EXPECT_EQ(nullptr, portal_detector_->http_request_);
    EXPECT_EQ(nullptr, portal_detector_->https_request_);
  }

  void ExpectHttpRequestSuccessWithStatus(int status_code) {
    EXPECT_CALL(*http_probe_connection_, GetResponseStatusCode())
        .WillOnce(Return(status_code));
    auto response =
        std::make_unique<brillo::http::Response>(http_probe_connection_);
    portal_detector_->ProcessHTTPProbeResult(std::move(response));
  }

  void HTTPRequestFailure(HttpRequest::Error error) {
    portal_detector_->ProcessHTTPProbeResult(base::unexpected(error));
  }

  void HTTPSRequestSuccess() {
    auto response =
        std::make_unique<brillo::http::Response>(https_probe_connection_);
    portal_detector_->ProcessHTTPSProbeResult(std::move(response));
  }

  void HTTPSRequestFailure(HttpRequest::Error error) {
    portal_detector_->ProcessHTTPSProbeResult(base::unexpected(error));
  }

 protected:
  StrictMock<MockEventDispatcher> dispatcher_;
  std::shared_ptr<brillo::http::MockTransport> http_probe_transport_;
  std::shared_ptr<brillo::http::MockConnection> http_probe_connection_;
  std::shared_ptr<brillo::http::MockTransport> https_probe_transport_;
  std::shared_ptr<brillo::http::MockConnection> https_probe_connection_;
  MockHttpRequest* http_request_ = nullptr;
  MockHttpRequest* https_request_ = nullptr;
  CallbackTarget callback_target_;
  const std::string interface_name_;
  std::vector<net_base::IPAddress> dns_servers_;
  std::unique_ptr<TestablePortalDetector> portal_detector_;
};

TEST_F(PortalDetectorTest, NoCustomCertificates) {
  std::vector<net_base::IPAddress> dns_list = {kDNSServer0, kDNSServer1};
  auto config = MakeProbingConfiguration();
  config.portal_https_url =
      *net_base::HttpUrl::CreateFromString(PortalDetector::kDefaultHttpsUrl);
  auto portal_detector =
      std::make_unique<TestablePortalDetector>(&dispatcher_, config);

  // First request for the HTTP probe: always set |allow_non_google_https| to
  // false. Second request for the HTTPS probe with the default URL: set
  // |allow_non_google_https| to false.
  EXPECT_CALL(
      *portal_detector,
      CreateHTTPRequest(kInterfaceName, net_base::IPFamily::kIPv4, dns_list,
                        /*allow_non_google_https=*/false))
      .WillOnce(Return(std::make_unique<MockHttpRequest>()))
      .WillOnce(Return(std::make_unique<MockHttpRequest>()));

  portal_detector->Start(net_base::IPFamily::kIPv4, dns_list,
                         base::DoNothing());
  portal_detector->Reset();
}

TEST_F(PortalDetectorTest, UseCustomCertificates) {
  std::vector<net_base::IPAddress> dns_list = {kDNSServer0, kDNSServer1};
  auto config = MakeProbingConfiguration();
  ASSERT_NE(config.portal_https_url, *net_base::HttpUrl::CreateFromString(
                                         PortalDetector::kDefaultHttpsUrl));
  auto portal_detector =
      std::make_unique<TestablePortalDetector>(&dispatcher_, config);

  // First request for the HTTP probe: always set |allow_non_google_https| to
  // false.
  EXPECT_CALL(
      *portal_detector,
      CreateHTTPRequest(kInterfaceName, net_base::IPFamily::kIPv4, dns_list,
                        /*allow_non_google_https=*/false))
      .WillOnce(Return(std::make_unique<MockHttpRequest>()));
  // Second request for the HTTPS probe with a non-default URL: set
  // |allow_non_google_https| to true.
  EXPECT_CALL(
      *portal_detector,
      CreateHTTPRequest(kInterfaceName, net_base::IPFamily::kIPv4, dns_list,
                        /*allow_non_google_https=*/true))
      .WillOnce(Return(std::make_unique<MockHttpRequest>()));

  portal_detector->Start(net_base::IPFamily::kIPv4, dns_list,
                         base::DoNothing());
  portal_detector->Reset();
}

TEST_F(PortalDetectorTest, Constructor) {
  ExpectReset();
}

TEST_F(PortalDetectorTest, IsInProgress) {
  // Before the trial is started, should not be active.
  EXPECT_FALSE(portal_detector()->IsRunning());

  // Once the trial is started, IsInProgress should return true.
  StartPortalRequest();
  EXPECT_TRUE(portal_detector()->IsRunning());

  // Finish the trial, IsInProgress should return false.
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kConnectionFailure,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  portal_detector()->StopTrialIfComplete(result);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, Restart) {
  EXPECT_FALSE(portal_detector()->IsRunning());

  EXPECT_EQ(0, portal_detector()->attempt_count());
  StartPortalRequest();
  EXPECT_EQ(portal_detector()->http_url_.ToString(), kHttpUrl);
  EXPECT_EQ(1, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  const PortalDetector::Result result = GetPortalRedirectResult(kHttpUrl);
  portal_detector()->StopTrialIfComplete(result);
  ExpectCleanupTrial();

  StartPortalRequest();
  EXPECT_EQ(2, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  portal_detector()->Reset();
  ExpectReset();
}

TEST_F(PortalDetectorTest, RestartAfterRedirect) {
  const std::optional<net_base::HttpUrl> probe_url =
      *net_base::HttpUrl::CreateFromString(kHttpUrl);

  EXPECT_FALSE(portal_detector()->IsRunning());
  EXPECT_EQ(0, portal_detector()->attempt_count());
  StartPortalRequest();
  EXPECT_EQ(1, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  const PortalDetector::Result result = GetPortalRedirectResult(kHttpUrl);
  portal_detector()->StopTrialIfComplete(result);
  ExpectCleanupTrial();

  StartPortalRequest();
  EXPECT_EQ(portal_detector()->http_url_, probe_url);
  EXPECT_EQ(2, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  portal_detector()->Reset();
  ExpectReset();
}

TEST_F(PortalDetectorTest, RestartAfterSuspectedRedirect) {
  const std::optional<net_base::HttpUrl> probe_url =
      *net_base::HttpUrl::CreateFromString(kHttpUrl);

  EXPECT_FALSE(portal_detector()->IsRunning());
  EXPECT_EQ(0, portal_detector()->attempt_count());
  StartPortalRequest();
  EXPECT_EQ(1, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kPortalSuspected,
      .http_status_code = 200,
      .http_content_length = 345,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
      .probe_url = probe_url,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kPortalSuspected,
            result.GetValidationState());

  portal_detector()->StopTrialIfComplete(result);
  ExpectCleanupTrial();

  StartPortalRequest();
  EXPECT_EQ(portal_detector()->http_url_, probe_url);
  EXPECT_EQ(2, portal_detector()->attempt_count());
  Mock::VerifyAndClearExpectations(&dispatcher_);

  portal_detector()->Reset();
  ExpectReset();
}

TEST_F(PortalDetectorTest, RestartWhileAlreadyInProgress) {
  EXPECT_FALSE(portal_detector()->IsRunning());

  EXPECT_EQ(0, portal_detector()->attempt_count());
  StartPortalRequest();
  EXPECT_EQ(1, portal_detector()->attempt_count());
  EXPECT_TRUE(portal_detector()->IsRunning());
  Mock::VerifyAndClearExpectations(portal_detector_.get());

  EXPECT_CALL(*portal_detector_, CreateHTTPRequest).Times(0);
  portal_detector_->Start(net_base::IPFamily::kIPv4, {kDNSServer0, kDNSServer1},
                          base::DoNothing());
  EXPECT_EQ(1, portal_detector()->attempt_count());
  EXPECT_TRUE(portal_detector()->IsRunning());
  Mock::VerifyAndClearExpectations(portal_detector_.get());

  portal_detector()->Reset();
  ExpectReset();
}

TEST_F(PortalDetectorTest, AttemptCount) {
  PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kDNSFailure,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  // The 1st attempt uses the default probing URLs.
  EXPECT_FALSE(portal_detector()->IsRunning());
  StartPortalRequest();
  EXPECT_EQ(portal_detector()->http_url_.ToString(), kHttpUrl);
  EXPECT_EQ(portal_detector()->https_url_.ToString(), kHttpsUrl);
  result.num_attempts = 1;
  EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
  portal_detector()->StopTrialIfComplete(result);
  EXPECT_EQ(1, portal_detector()->attempt_count());

  // The 2nd and so on attempts use the fallback or the default probing URLs.
  std::set<std::string> expected_retry_http_urls(kFallbackHttpUrls.begin(),
                                                 kFallbackHttpUrls.end());
  expected_retry_http_urls.insert(kHttpUrl);

  std::set<std::string> expected_retry_https_urls(kFallbackHttpsUrls.begin(),
                                                  kFallbackHttpsUrls.end());
  expected_retry_https_urls.insert(kHttpsUrl);
  for (int i = 2; i < 10; i++) {
    result.num_attempts = i;
    EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
    StartPortalRequest();
    EXPECT_EQ(i, portal_detector()->attempt_count());

    EXPECT_NE(
        expected_retry_http_urls.find(portal_detector()->http_url_.ToString()),
        expected_retry_http_urls.end());
    EXPECT_NE(expected_retry_https_urls.find(
                  portal_detector()->https_url_.ToString()),
              expected_retry_https_urls.end());

    portal_detector()->StopTrialIfComplete(result);
    Mock::VerifyAndClearExpectations(&callback_target_);
  }

  portal_detector()->Reset();
  ExpectReset();
}

TEST_F(PortalDetectorTest, RequestSuccess) {
  StartPortalRequest();

  EXPECT_CALL(callback_target(), ResultCallback).Times(0);
  EXPECT_TRUE(portal_detector_->IsRunning());
  EXPECT_NE(nullptr, portal_detector_->http_request_);
  EXPECT_NE(nullptr, portal_detector_->https_request_);

  // HTTPS probe does not trigger anything (for now)
  HTTPSRequestSuccess();
  Mock::VerifyAndClearExpectations(&callback_target_);

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kSuccess,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(204);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, RequestHTTPFailureHTTPSSuccess) {
  StartPortalRequest();

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kFailure,
      .http_status_code = 123,
      .http_content_length = 10,
      .https_result = PortalDetector::ProbeResult::kSuccess,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target(), ResultCallback(Eq(result))).Times(0);
  EXPECT_TRUE(portal_detector_->IsRunning());
  EXPECT_NE(nullptr, portal_detector_->http_request_);
  EXPECT_NE(nullptr, portal_detector_->https_request_);

  EXPECT_CALL(*http_connection(), GetResponseHeader("Content-Length"))
      .WillOnce(Return("10"));
  ExpectHttpRequestSuccessWithStatus(123);
  EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
  HTTPSRequestSuccess();
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, RequestHTTPSuccessHTTPSFailure) {
  StartPortalRequest();

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kTLSFailure,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  EXPECT_TRUE(portal_detector_->IsRunning());
  EXPECT_CALL(*http_connection(), GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(204);
  EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
  HTTPSRequestFailure(HttpRequest::Error::kTLSFailure);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, RequestFail) {
  StartPortalRequest();

  // HTTPS probe does not trigger anything (for now)
  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kFailure,
      .http_status_code = 123,
      .http_content_length = 10,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target(), ResultCallback(Eq(result))).Times(0);
  EXPECT_TRUE(portal_detector_->IsRunning());
  EXPECT_NE(nullptr, portal_detector_->http_request_);
  EXPECT_NE(nullptr, portal_detector_->https_request_);

  EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Content-Length"))
      .WillOnce(Return("10"));
  ExpectHttpRequestSuccessWithStatus(123);
  HTTPSRequestFailure(HttpRequest::Error::kConnectionFailure);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, RequestRedirect) {
  StartPortalRequest();

  EXPECT_CALL(callback_target(), ResultCallback).Times(0);
  EXPECT_TRUE(portal_detector_->IsRunning());
  EXPECT_NE(nullptr, portal_detector_->http_request_);
  EXPECT_NE(nullptr, portal_detector_->https_request_);
  HTTPSRequestFailure(HttpRequest::Error::kConnectionFailure);
  Mock::VerifyAndClearExpectations(&callback_target_);

  const PortalDetector::Result result = GetPortalRedirectResult(kHttpUrl);
  EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Location"))
      .WillOnce(Return(std::string(kPortalSignInURL)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(302);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, RequestTempRedirect) {
  StartPortalRequest();

  EXPECT_CALL(callback_target(), ResultCallback).Times(0);
  EXPECT_TRUE(portal_detector_->IsRunning());
  EXPECT_NE(nullptr, portal_detector_->http_request_);
  EXPECT_NE(nullptr, portal_detector_->https_request_);
  HTTPSRequestFailure(HttpRequest::Error::kConnectionFailure);
  Mock::VerifyAndClearExpectations(&callback_target_);

  PortalDetector::Result result = GetPortalRedirectResult(kHttpUrl);
  result.http_status_code = 307;
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kPortalRedirect,
            result.GetValidationState());

  EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Location"))
      .WillOnce(Return(std::string(kPortalSignInURL)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(307);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, RequestRedirectWithHTTPSProbeTimeout) {
  StartPortalRequest();
  EXPECT_TRUE(portal_detector_->IsRunning());

  PortalDetector::Result result = GetPortalRedirectResult(kHttpUrl);
  result.https_result = PortalDetector::ProbeResult::kNoResult;
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_FALSE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kPortalRedirect,
            result.GetValidationState());

  EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Location"))
      .WillOnce(Return(std::string(kPortalSignInURL)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(302);
  // The HTTPS probe does not complete.
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, Request200AndInvalidContentLength) {
  StartPortalRequest();
  EXPECT_TRUE(portal_detector_->IsRunning());

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kFailure,
      .http_status_code = 200,
      .http_content_length = std::nullopt,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Content-Length"))
      .WillOnce(Return("invalid"));
  ExpectHttpRequestSuccessWithStatus(200);
  HTTPSRequestFailure(HttpRequest::Error::kConnectionFailure);
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, Request200WithoutContent) {
  StartPortalRequest();
  EXPECT_TRUE(portal_detector_->IsRunning());

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 200,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kSuccess,
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_TRUE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            result.GetValidationState());

  EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  ExpectHttpRequestSuccessWithStatus(200);
  HTTPSRequestSuccess();
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, Request200WithContent) {
  StartPortalRequest();
  EXPECT_TRUE(portal_detector_->IsRunning());

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kPortalSuspected,
      .http_status_code = 200,
      .http_content_length = 768,
      .probe_url = *net_base::HttpUrl::CreateFromString(kHttpUrl),
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_FALSE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kPortalSuspected,
            result.GetValidationState());

  EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Content-Length"))
      .WillOnce(Return("768"));
  ExpectHttpRequestSuccessWithStatus(200);
  // The trial has been completed, even if the HTTPS probe did not complete.
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, RequestInvalidRedirect) {
  StartPortalRequest();
  EXPECT_TRUE(portal_detector_->IsRunning());

  const PortalDetector::Result result = {
      .num_attempts = 1,
      .http_result = PortalDetector::ProbeResult::kPortalInvalidRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .redirect_url = std::nullopt,
      .probe_url = *net_base::HttpUrl::CreateFromString(kHttpUrl),
  };
  ASSERT_TRUE(result.IsHTTPProbeComplete());
  ASSERT_FALSE(result.IsHTTPSProbeComplete());
  ASSERT_EQ(PortalDetector::ValidationState::kPortalSuspected,
            result.GetValidationState());

  EXPECT_CALL(callback_target(), ResultCallback(Eq(result)));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Content-Length"))
      .WillOnce(Return("0"));
  EXPECT_CALL(*http_connection(), GetResponseHeader("Location"))
      .WillOnce(Return("invalid_url"));
  ExpectHttpRequestSuccessWithStatus(302);
  // The trial has been completed, even if the HTTPS probe did not complete.
  ExpectCleanupTrial();
}

TEST_F(PortalDetectorTest, PickProbeURLs) {
  const auto url1 = *net_base::HttpUrl::CreateFromString("http://www.url1.com");
  const auto url2 = *net_base::HttpUrl::CreateFromString("http://www.url2.com");
  const auto url3 = *net_base::HttpUrl::CreateFromString("http://www.url3.com");
  const std::set<std::string> all_urls = {url1.ToString(), url2.ToString(),
                                          url3.ToString()};
  std::set<std::string> all_found_urls;

  EXPECT_EQ(url1, portal_detector_->PickProbeUrl(url1, {}));
  EXPECT_EQ(url1, portal_detector_->PickProbeUrl(url1, {url2, url3}));

  // The loop index starts at 1 to force a non-zero |attempt_count_| and to
  // force using the fallback list.
  for (int i = 1; i < 100; i++) {
    portal_detector_->attempt_count_ = i;
    EXPECT_EQ(portal_detector_->PickProbeUrl(url1, {}), url1);

    const auto& found =
        portal_detector_->PickProbeUrl(url1, {url2, url3}).ToString();
    if (i == 1) {
      EXPECT_EQ(url2.ToString(), found);
    } else if (i == 2) {
      EXPECT_EQ(url3.ToString(), found);
    } else {
      all_found_urls.insert(found);
    }
    EXPECT_NE(all_urls.find(found), all_urls.end());
  }
  // Probability this assert fails = 3 * 1/3 ^ 97 + 3 * 2/3 ^ 97
  EXPECT_EQ(all_urls, all_found_urls);
}

TEST(PortalDetectorResultTest, HTTPSTimeout) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kHTTPTimeout,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kNoConnectivity);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultHTTPSFailure);
}

TEST(PortalDetectorResultTest, PartialConnectivity) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kNoConnectivity);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultHTTPSFailure);
}

TEST(PortalDetectorResultTest, NoConnectivity) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kConnectionFailure,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
      .http_duration = base::Milliseconds(0),
      .https_duration = base::Milliseconds(200),
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kNoConnectivity);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultConnectionFailure);
}

TEST(PortalDetectorResultTest, InternetConnectivity) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kSuccess,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kInternetConnectivity);
  EXPECT_EQ(result.GetResultMetric(), Metrics::kPortalDetectorResultOnline);
}

TEST(PortalDetectorResultTest, PortalRedirect) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kPortalRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .redirect_url =
          net_base::HttpUrl::CreateFromString("https://portal.com/login"),
      .probe_url = net_base::HttpUrl::CreateFromString(
          "https://service.google.com/generate_204"),
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kPortalRedirect);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultRedirectFound);
}

TEST(PortalDetectorResultTest, PortalInvalidRedirect) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kPortalInvalidRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
      .redirect_url = std::nullopt,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kPortalSuspected);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultRedirectNoUrl);
}

TEST(PortalDetectorResultTest, Empty200) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 200,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kSuccess,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kInternetConnectivity);
  EXPECT_EQ(result.GetResultMetric(), Metrics::kPortalDetectorResultOnline);
}

TEST(PortalDetectorResultTest, PortalSuspected200) {
  const PortalDetector::Result result = {
      .http_result = PortalDetector::ProbeResult::kPortalSuspected,
      .http_status_code = 200,
      .http_content_length = 1023,
      .https_result = PortalDetector::ProbeResult::kTLSFailure,
  };

  EXPECT_EQ(result.GetValidationState(),
            PortalDetector::ValidationState::kPortalSuspected);
  EXPECT_EQ(result.GetResultMetric(),
            Metrics::kPortalDetectorResultHTTPSFailure);
}

}  // namespace shill
