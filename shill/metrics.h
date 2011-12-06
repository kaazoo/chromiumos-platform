// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_METRICS_
#define SHILL_METRICS_

#include <list>

#include <base/lazy_instance.h>
#include <base/memory/scoped_vector.h>
#include <metrics/metrics_library.h>
#include <metrics/timer.h>

#include "shill/refptr_types.h"
#include "shill/service.h"

namespace shill {

class WiFiService;

class Metrics {
 public:
  enum WiFiChannel {
    kWiFiChannelUndef = 0,
    kWiFiChannel2412 = 1,
    kWiFiChannel2417 = 2,
    kWiFiChannel2422 = 3,
    kWiFiChannel2427 = 4,
    kWiFiChannel2432 = 5,
    kWiFiChannel2437 = 6,
    kWiFiChannel2442 = 7,
    kWiFiChannel2447 = 8,
    kWiFiChannel2452 = 9,
    kWiFiChannel2457 = 10,
    kWiFiChannel2462 = 11,
    kWiFiChannel2467 = 12,
    kWiFiChannel2472 = 13,
    kWiFiChannel2484 = 14,

    kWiFiChannel5180 = 15,
    kWiFiChannel5200 = 16,
    kWiFiChannel5220 = 17,
    kWiFiChannel5240 = 18,
    kWiFiChannel5260 = 19,
    kWiFiChannel5280 = 20,
    kWiFiChannel5300 = 21,
    kWiFiChannel5320 = 22,

    kWiFiChannel5500 = 23,
    kWiFiChannel5520 = 24,
    kWiFiChannel5540 = 25,
    kWiFiChannel5560 = 26,
    kWiFiChannel5580 = 27,
    kWiFiChannel5600 = 28,
    kWiFiChannel5620 = 29,
    kWiFiChannel5640 = 30,
    kWiFiChannel5660 = 31,
    kWiFiChannel5680 = 32,
    kWiFiChannel5700 = 33,

    kWiFiChannel5745 = 34,
    kWiFiChannel5765 = 35,
    kWiFiChannel5785 = 36,
    kWiFiChannel5805 = 37,
    kWiFiChannel5825 = 38,

    kWiFiChannel5170 = 39,
    kWiFiChannel5190 = 40,
    kWiFiChannel5210 = 41,
    kWiFiChannel5230 = 42,

    /* NB: ignore old 11b bands 2312..2372 and 2512..2532 */
    /* NB: ignore regulated bands 4920..4980 and 5020..5160 */
    kWiFiChannelMax
  };

  static const char kMetricNetworkChannel[];
  static const int kMetricNetworkChannelMax;
  static const char kMetricNetworkServiceErrors[];
  static const int kMetricNetworkServiceErrorsMax;
  static const char kMetricTimeToConfigMilliseconds[];
  static const char kMetricTimeToJoinMilliseconds[];
  static const char kMetricTimeToOnlineMilliseconds[];
  static const char kMetricTimeToPortalMilliseconds[];
  static const int kTimerHistogramMaxMilliseconds;
  static const int kTimerHistogramMinMilliseconds;
  static const int kTimerHistogramNumBuckets;

  virtual ~Metrics();

  // This is a singleton -- use Metrics::GetInstance()->Foo()
  static Metrics *GetInstance();

  // Converts the WiFi frequency into the associated UMA channel enumerator.
  static WiFiChannel WiFiFrequencyToChannel(uint16 frequency);

  // Registers a service with this object so it can use the timers to track
  // state transition metrics.
  void RegisterService(const Service *service);

  // Deregisters the service from this class.  All state transition timers
  // will be removed.
  void DeregisterService(const Service *service);

  // Tracks the time it takes |service| to go from |start_state| to
  // |stop_state|.  When |stop_state| is reached, the time is sent to UMA.
  void AddServiceStateTransitionTimer(const Service *service,
                                      const std::string &histogram_name,
                                      Service::ConnectState start_state,
                                      Service::ConnectState stop_state);

  // Specializes |metric_name| for the specified |technology_id|.
  std::string GetFullMetricName(const char *metric_name,
                                Technology::Identifier technology_id);

  // Notifies this object that the default service has changed.
  // |service| is the new default service.
  void NotifyDefaultServiceChanged(const Service *service);

  // Notifies this object that |service| state has changed.
  virtual void NotifyServiceStateChanged(const Service *service,
                                         Service::ConnectState new_state);

  // Notifies this object that |service| has been disconnected and whether
  // the disconnect was requested by the user or not.
  void NotifyServiceDisconnect(const Service *service,
                               bool manual_disconnect);

  // Notifies this object of a power management event.
  void NotifyPower();

  // Sends linear histogram data to UMA.
  bool SendEnumToUMA(const std::string &name, int sample, int max);

 private:
  friend struct base::DefaultLazyInstanceTraits<Metrics>;
  friend class MetricsTest;
  FRIEND_TEST(MetricsTest, TimeToConfig);
  FRIEND_TEST(MetricsTest, TimeToPortal);
  FRIEND_TEST(MetricsTest, TimeToOnline);
  FRIEND_TEST(MetricsTest, ServiceFailure);
  FRIEND_TEST(MetricsTest, WiFiServiceChannel);
  FRIEND_TEST(MetricsTest, FrequencyToChannel);

  typedef ScopedVector<chromeos_metrics::TimerReporter> TimerReporters;
  typedef std::list<chromeos_metrics::TimerReporter *> TimerReportersList;
  typedef std::map<Service::ConnectState, TimerReportersList>
      TimerReportersByState;
  struct ServiceMetrics {
    ServiceMetrics() : service(NULL) {}
    // The service is registered/deregistered in the Service
    // constructor/destructor, therefore there is no need to keep a ref count.
    const Service *service;
    // All TimerReporter objects are stored in |timers| which owns the objects.
    // |start_on_state| and |stop_on_state| contain pointers to the
    // TimerReporter objects and control when to start and stop the timers.
    TimerReporters timers;
    TimerReportersByState start_on_state;
    TimerReportersByState stop_on_state;
  };
  typedef std::map<const Service *, std::tr1::shared_ptr<ServiceMetrics> >
      ServiceMetricsLookupMap;

  static const uint16 kWiFiBandwidth5MHz;
  static const uint16 kWiFiBandwidth20MHz;
  static const uint16 kWiFiFrequency2412;
  static const uint16 kWiFiFrequency2472;
  static const uint16 kWiFiFrequency2484;
  static const uint16 kWiFiFrequency5170;
  static const uint16 kWiFiFrequency5180;
  static const uint16 kWiFiFrequency5230;
  static const uint16 kWiFiFrequency5240;
  static const uint16 kWiFiFrequency5320;
  static const uint16 kWiFiFrequency5500;
  static const uint16 kWiFiFrequency5700;
  static const uint16 kWiFiFrequency5745;
  static const uint16 kWiFiFrequency5825;

  Metrics();

  void InitializeCommonServiceMetrics(const Service *service);
  void UpdateServiceStateTransitionMetrics(ServiceMetrics *service_metrics,
                                           Service::ConnectState new_state);
  void SendServiceFailure(const Service *service);

  // For unit test purposes.
  void set_library(MetricsLibraryInterface *library);

  // |library_| points to |metrics_library_| when shill runs normally.
  // However, in order to allow for unit testing, we point |library_| to a
  // MetricsLibraryMock object instead.
  MetricsLibrary metrics_library_;
  MetricsLibraryInterface *library_;
  ServiceMetricsLookupMap services_metrics_;

  DISALLOW_COPY_AND_ASSIGN(Metrics);
};

}  // namespace shill

#endif  // SHILL_METRICS_
