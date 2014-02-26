// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/omaha_request_action.h"

#include <inttypes.h>

#include <sstream>
#include <string>

#include <base/bind.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "update_engine/action_pipe.h"
#include "update_engine/constants.h"
#include "update_engine/hardware_interface.h"
#include "update_engine/omaha_hash_calculator.h"
#include "update_engine/omaha_request_params.h"
#include "update_engine/p2p_manager.h"
#include "update_engine/payload_state_interface.h"
#include "update_engine/prefs_interface.h"
#include "update_engine/utils.h"

using base::Time;
using base::TimeDelta;
using std::string;

namespace chromeos_update_engine {

// List of custom pair tags that we interpret in the Omaha Response:
static const char* kTagDeadline = "deadline";
static const char* kTagDisablePayloadBackoff = "DisablePayloadBackoff";
static const char* kTagVersion = "version";
// Deprecated: "IsDelta"
static const char* kTagIsDeltaPayload = "IsDeltaPayload";
static const char* kTagMaxFailureCountPerUrl = "MaxFailureCountPerUrl";
static const char* kTagMaxDaysToScatter = "MaxDaysToScatter";
// Deprecated: "ManifestSignatureRsa"
// Deprecated: "ManifestSize"
static const char* kTagMetadataSignatureRsa = "MetadataSignatureRsa";
static const char* kTagMetadataSize = "MetadataSize";
static const char* kTagMoreInfo = "MoreInfo";
// Deprecated: "NeedsAdmin"
static const char* kTagPrompt = "Prompt";
static const char* kTagSha256 = "sha256";
static const char* kTagDisableP2PForDownloading = "DisableP2PForDownloading";
static const char* kTagDisableP2PForSharing = "DisableP2PForSharing";
static const char* kTagPublicKeyRsa = "PublicKeyRsa";

namespace {

const string kGupdateVersion("ChromeOSUpdateEngine-0.1.0.0");

// This is handy for passing strings into libxml2
#define ConstXMLStr(x) (reinterpret_cast<const xmlChar*>(x))

// These are for scoped_ptr_malloc, which is like scoped_ptr, but allows
// a custom free() function to be specified.
class ScopedPtrXmlDocFree {
 public:
  inline void operator()(void* x) const {
    xmlFreeDoc(reinterpret_cast<xmlDoc*>(x));
  }
};
class ScopedPtrXmlFree {
 public:
  inline void operator()(void* x) const {
    xmlFree(x);
  }
};
class ScopedPtrXmlXPathObjectFree {
 public:
  inline void operator()(void* x) const {
    xmlXPathFreeObject(reinterpret_cast<xmlXPathObject*>(x));
  }
};
class ScopedPtrXmlXPathContextFree {
 public:
  inline void operator()(void* x) const {
    xmlXPathFreeContext(reinterpret_cast<xmlXPathContext*>(x));
  }
};

// Returns true if |ping_days| has a value that needs to be sent,
// false otherwise.
bool ShouldPing(int ping_days) {
  return ping_days > 0 || ping_days == OmahaRequestAction::kNeverPinged;
}

// Returns an XML ping element attribute assignment with attribute
// |name| and value |ping_days| if |ping_days| has a value that needs
// to be sent, or an empty string otherwise.
string GetPingAttribute(const string& name, int ping_days) {
  if (ShouldPing(ping_days)) {
    return base::StringPrintf(" %s=\"%d\"", name.c_str(), ping_days);
  }
  return "";
}

// Returns an XML ping element if any of the elapsed days need to be
// sent, or an empty string otherwise.
string GetPingXml(int ping_active_days, int ping_roll_call_days) {
  string ping_active = GetPingAttribute("a", ping_active_days);
  string ping_roll_call = GetPingAttribute("r", ping_roll_call_days);
  if (!ping_active.empty() || !ping_roll_call.empty()) {
    return base::StringPrintf("        <ping active=\"1\"%s%s></ping>\n",
                        ping_active.c_str(),
                        ping_roll_call.c_str());
  }
  return "";
}

// Returns an XML that goes into the body of the <app> element of the Omaha
// request based on the given parameters.
string GetAppBody(const OmahaEvent* event,
                  OmahaRequestParams* params,
                  bool ping_only,
                  int ping_active_days,
                  int ping_roll_call_days,
                  PrefsInterface* prefs) {
  string app_body;
  if (event == NULL) {
    app_body = GetPingXml(ping_active_days, ping_roll_call_days);
    if (!ping_only) {
      // not passing update_disabled to Omaha because we want to
      // get the update and report with UpdateDeferred result so that
      // borgmon charts show up updates that are deferred. This is also
      // the expected behavior when we move to Omaha v3.0 protocol, so it'll
      // be consistent.
      app_body += base::StringPrintf(
          "        <updatecheck targetversionprefix=\"%s\""
          "></updatecheck>\n",
          XmlEncode(params->target_version_prefix()).c_str());

      // If this is the first update check after a reboot following a previous
      // update, generate an event containing the previous version number. If
      // the previous version preference file doesn't exist the event is still
      // generated with a previous version of 0.0.0.0 -- this is relevant for
      // older clients or new installs. The previous version event is not sent
      // for ping-only requests because they come before the client has
      // rebooted.
      string prev_version;
      if (!prefs->GetString(kPrefsPreviousVersion, &prev_version)) {
        prev_version = "0.0.0.0";
      }

      app_body += base::StringPrintf(
          "        <event eventtype=\"%d\" eventresult=\"%d\" "
          "previousversion=\"%s\"></event>\n",
          OmahaEvent::kTypeUpdateComplete,
          OmahaEvent::kResultSuccessReboot,
          XmlEncode(prev_version).c_str());
      LOG_IF(WARNING, !prefs->SetString(kPrefsPreviousVersion, ""))
          << "Unable to reset the previous version.";
    }
  } else {
    // The error code is an optional attribute so append it only if the result
    // is not success.
    string error_code;
    if (event->result != OmahaEvent::kResultSuccess) {
      error_code = base::StringPrintf(" errorcode=\"%d\"", event->error_code);
    }
    app_body = base::StringPrintf(
        "        <event eventtype=\"%d\" eventresult=\"%d\"%s></event>\n",
        event->type, event->result, error_code.c_str());
  }

  return app_body;
}

// Returns an XML that corresponds to the entire <app> node of the Omaha
// request based on the given parameters.
string GetAppXml(const OmahaEvent* event,
                 OmahaRequestParams* params,
                 bool ping_only,
                 int ping_active_days,
                 int ping_roll_call_days,
                 int install_date_in_days,
                 SystemState* system_state) {
  string app_body = GetAppBody(event, params, ping_only, ping_active_days,
                               ping_roll_call_days, system_state->prefs());
  string app_versions;

  // If we are upgrading to a more stable channel and we are allowed to do
  // powerwash, then pass 0.0.0.0 as the version. This is needed to get the
  // highest-versioned payload on the destination channel.
  if (params->to_more_stable_channel() && params->is_powerwash_allowed()) {
    LOG(INFO) << "Passing OS version as 0.0.0.0 as we are set to powerwash "
              << "on downgrading to the version in the more stable channel";
    app_versions = "version=\"0.0.0.0\" from_version=\"" +
      XmlEncode(params->app_version()) + "\" ";
  } else {
    app_versions = "version=\"" + XmlEncode(params->app_version()) + "\" ";
  }

  string download_channel = params->download_channel();
  string app_channels = "track=\"" + XmlEncode(download_channel) + "\" ";
  if (params->current_channel() != download_channel)
     app_channels +=
       "from_track=\"" + XmlEncode(params->current_channel()) + "\" ";

  string delta_okay_str = params->delta_okay() ? "true" : "false";

  // If install_date_days is not set (e.g. its value is -1 ), don't
  // include the attribute.
  string install_date_in_days_str = "";
  if (install_date_in_days >= 0) {
    install_date_in_days_str = base::StringPrintf("installdate=\"%d\" ",
                                                  install_date_in_days);
  }

  string app_xml =
      "    <app appid=\"" + XmlEncode(params->GetAppId()) + "\" " +
                app_versions +
                app_channels +
                "lang=\"" + XmlEncode(params->app_lang()) + "\" " +
                "board=\"" + XmlEncode(params->os_board()) + "\" " +
                "hardware_class=\"" + XmlEncode(params->hwid()) + "\" " +
                "delta_okay=\"" + delta_okay_str + "\" "
                "fw_version=\"" + XmlEncode(params->fw_version()) + "\" " +
                "ec_version=\"" + XmlEncode(params->ec_version()) + "\" " +
                install_date_in_days_str +
                ">\n" +
                   app_body +
      "    </app>\n";

  return app_xml;
}

// Returns an XML that corresponds to the entire <os> node of the Omaha
// request based on the given parameters.
string GetOsXml(OmahaRequestParams* params) {
  string os_xml =
      "    <os version=\"" + XmlEncode(params->os_version()) + "\" " +
               "platform=\"" + XmlEncode(params->os_platform()) + "\" " +
               "sp=\"" + XmlEncode(params->os_sp()) + "\">"
          "</os>\n";
  return os_xml;
}

// Returns an XML that corresponds to the entire Omaha request based on the
// given parameters.
string GetRequestXml(const OmahaEvent* event,
                     OmahaRequestParams* params,
                     bool ping_only,
                     int ping_active_days,
                     int ping_roll_call_days,
                     int install_date_in_days,
                     SystemState* system_state) {
  string os_xml = GetOsXml(params);
  string app_xml = GetAppXml(event, params, ping_only, ping_active_days,
                             ping_roll_call_days, install_date_in_days,
                             system_state);

  string install_source = base::StringPrintf("installsource=\"%s\" ",
      (params->interactive() ? "ondemandupdate" : "scheduler"));

  string request_xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<request protocol=\"3.0\" "
                "version=\"" + XmlEncode(kGupdateVersion) + "\" "
                "updaterversion=\"" + XmlEncode(kGupdateVersion) + "\" " +
                install_source +
                "ismachine=\"1\">\n" +
                  os_xml +
                  app_xml +
      "</request>\n";

  return request_xml;
}

}  // namespace {}

// Encodes XML entities in a given string with libxml2. input must be
// UTF-8 formatted. Output will be UTF-8 formatted.
string XmlEncode(const string& input) {
  //  // TODO(adlr): if allocating a new xmlDoc each time is taking up too much
  //  // cpu, considering creating one and caching it.
  //  scoped_ptr_malloc<xmlDoc, ScopedPtrXmlDocFree> xml_doc(
  //      xmlNewDoc(ConstXMLStr("1.0")));
  //  if (!xml_doc.get()) {
  //    LOG(ERROR) << "Unable to create xmlDoc";
  //    return "";
  //  }
  scoped_ptr_malloc<xmlChar, ScopedPtrXmlFree> str(
      xmlEncodeEntitiesReentrant(NULL, ConstXMLStr(input.c_str())));
  return string(reinterpret_cast<const char *>(str.get()));
}

OmahaRequestAction::OmahaRequestAction(SystemState* system_state,
                                       OmahaEvent* event,
                                       HttpFetcher* http_fetcher,
                                       bool ping_only)
    : system_state_(system_state),
      event_(event),
      http_fetcher_(http_fetcher),
      ping_only_(ping_only),
      ping_active_days_(0),
      ping_roll_call_days_(0) {
  params_ = system_state->request_params();
}

OmahaRequestAction::~OmahaRequestAction() {}

// Calculates the value to use for the ping days parameter.
int OmahaRequestAction::CalculatePingDays(const string& key) {
  int days = kNeverPinged;
  int64_t last_ping = 0;
  if (system_state_->prefs()->GetInt64(key, &last_ping) && last_ping >= 0) {
    days = (Time::Now() - Time::FromInternalValue(last_ping)).InDays();
    if (days < 0) {
      // If |days| is negative, then the system clock must have jumped
      // back in time since the ping was sent. Mark the value so that
      // it doesn't get sent to the server but we still update the
      // last ping daystart preference. This way the next ping time
      // will be correct, hopefully.
      days = kPingTimeJump;
      LOG(WARNING) <<
          "System clock jumped back in time. Resetting ping daystarts.";
    }
  }
  return days;
}

void OmahaRequestAction::InitPingDays() {
  // We send pings only along with update checks, not with events.
  if (IsEvent()) {
    return;
  }
  // TODO(petkov): Figure a way to distinguish active use pings
  // vs. roll call pings. Currently, the two pings are identical. A
  // fix needs to change this code as well as UpdateLastPingDays.
  ping_active_days_ = CalculatePingDays(kPrefsLastActivePingDay);
  ping_roll_call_days_ = CalculatePingDays(kPrefsLastRollCallPingDay);
}

// static
int OmahaRequestAction::GetInstallDate(SystemState* system_state) {
  PrefsInterface* prefs = system_state->prefs();
  if (prefs == NULL)
    return -1;

  // If we have the value stored on disk, just return it.
  int64_t stored_value;
  if (prefs->GetInt64(kPrefsInstallDateDays, &stored_value)) {
    // Convert and sanity-check.
    int install_date_days = static_cast<int>(stored_value);
    if (install_date_days >= 0)
      return install_date_days;
    LOG(ERROR) << "Dropping stored Omaha InstallData since its value num_days="
               << install_date_days << " looks suspicious.";
    prefs->Delete(kPrefsInstallDateDays);
  }

  // Otherwise, if OOBE is not complete then do nothing and wait for
  // ParseResponse() to call ParseInstallDate() and then
  // PersistInstallDate() to set the kPrefsInstallDateDays state
  // variable. Once that is done, we'll then report back in future
  // Omaha requests.  This works exactly because OOBE triggers an
  // update check.
  //
  // However, if OOBE is complete and the kPrefsInstallDateDays state
  // variable is not set, there are two possibilities
  //
  //   1. The update check in OOBE failed so we never got a response
  //      from Omaha (no network etc.); or
  //
  //   2. OOBE was done on an older version that didn't write to the
  //      kPrefsInstallDateDays state variable.
  //
  // In both cases, we approximate the install date by simply
  // inspecting the timestamp of when OOBE happened.

  Time time_of_oobe;
  if (!system_state->IsOOBEComplete(&time_of_oobe)) {
    LOG(INFO) << "Not generating Omaha InstallData as we have "
              << "no prefs file and OOBE is not complete.";
    return -1;
  }

  int num_days;
  if (!utils::ConvertToOmahaInstallDate(time_of_oobe, &num_days)) {
    LOG(ERROR) << "Not generating Omaha InstallData from time of OOBE "
               << "as its value '" << utils::ToString(time_of_oobe)
               << "' looks suspicious.";
    return -1;
  }

  // Persist this to disk, for future use.
  if (!OmahaRequestAction::PersistInstallDate(system_state,
                                              num_days,
                                              kProvisionedFromOOBEMarker))
    return -1;

  LOG(INFO) << "Set the Omaha InstallDate from OOBE time-stamp to "
            << num_days << " days";

  return num_days;
}

void OmahaRequestAction::PerformAction() {
  http_fetcher_->set_delegate(this);
  InitPingDays();
  if (ping_only_ &&
      !ShouldPing(ping_active_days_) &&
      !ShouldPing(ping_roll_call_days_)) {
    processor_->ActionComplete(this, kErrorCodeSuccess);
    return;
  }

  string request_post(GetRequestXml(event_.get(),
                                    params_,
                                    ping_only_,
                                    ping_active_days_,
                                    ping_roll_call_days_,
                                    GetInstallDate(system_state_),
                                    system_state_));

  http_fetcher_->SetPostData(request_post.data(), request_post.size(),
                             kHttpContentTypeTextXml);
  LOG(INFO) << "Posting an Omaha request to " << params_->update_url();
  LOG(INFO) << "Request: " << request_post;
  http_fetcher_->BeginTransfer(params_->update_url());
}

void OmahaRequestAction::TerminateProcessing() {
  http_fetcher_->TerminateTransfer();
}

// We just store the response in the buffer. Once we've received all bytes,
// we'll look in the buffer and decide what to do.
void OmahaRequestAction::ReceivedBytes(HttpFetcher *fetcher,
                                       const char* bytes,
                                       int length) {
  response_buffer_.reserve(response_buffer_.size() + length);
  response_buffer_.insert(response_buffer_.end(), bytes, bytes + length);
}

namespace {
// If non-NULL response, caller is responsible for calling xmlXPathFreeObject()
// on the returned object.
// This code is roughly based on the libxml tutorial at:
// http://xmlsoft.org/tutorial/apd.html
xmlXPathObject* GetNodeSet(xmlDoc* doc, const xmlChar* xpath) {
  xmlXPathObject* result = NULL;

  scoped_ptr_malloc<xmlXPathContext, ScopedPtrXmlXPathContextFree> context(
      xmlXPathNewContext(doc));
  if (!context.get()) {
    LOG(ERROR) << "xmlXPathNewContext() returned NULL";
    return NULL;
  }

  result = xmlXPathEvalExpression(xpath, context.get());
  if (result == NULL) {
    LOG(ERROR) << "Unable to find " << xpath << " in XML document";
    return NULL;
  }
  if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
    LOG(INFO) << "Nodeset is empty for " << xpath;
    xmlXPathFreeObject(result);
    return NULL;
  }
  return result;
}

// Returns the string value of a named attribute on a node, or empty string
// if no such node exists. If the attribute exists and has a value of
// empty string, there's no way to distinguish that from the attribute
// not existing.
string XmlGetProperty(xmlNode* node, const char* name) {
  if (!xmlHasProp(node, ConstXMLStr(name)))
    return "";
  scoped_ptr_malloc<xmlChar, ScopedPtrXmlFree> str(
      xmlGetProp(node, ConstXMLStr(name)));
  string ret(reinterpret_cast<const char *>(str.get()));
  return ret;
}

// Parses a 64 bit base-10 int from a string and returns it. Returns 0
// on error. If the string contains "0", that's indistinguishable from
// error.
off_t ParseInt(const string& str) {
  off_t ret = 0;
  int rc = sscanf(str.c_str(), "%" PRIi64, &ret);
  if (rc < 1) {
    // failure
    return 0;
  }
  return ret;
}

// Update the last ping day preferences based on the server daystart
// response. Returns true on success, false otherwise.
bool UpdateLastPingDays(xmlDoc* doc, PrefsInterface* prefs) {
  static const char kDaystartNodeXpath[] = "/response/daystart";

  scoped_ptr_malloc<xmlXPathObject, ScopedPtrXmlXPathObjectFree>
      xpath_nodeset(GetNodeSet(doc, ConstXMLStr(kDaystartNodeXpath)));
  TEST_AND_RETURN_FALSE(xpath_nodeset.get());
  xmlNodeSet* nodeset = xpath_nodeset->nodesetval;
  TEST_AND_RETURN_FALSE(nodeset && nodeset->nodeNr >= 1);
  xmlNode* daystart_node = nodeset->nodeTab[0];
  TEST_AND_RETURN_FALSE(xmlHasProp(daystart_node,
                                   ConstXMLStr("elapsed_seconds")));

  int64_t elapsed_seconds = 0;
  TEST_AND_RETURN_FALSE(base::StringToInt64(XmlGetProperty(daystart_node,
                                                           "elapsed_seconds"),
                                            &elapsed_seconds));
  TEST_AND_RETURN_FALSE(elapsed_seconds >= 0);

  // Remember the local time that matches the server's last midnight
  // time.
  Time daystart = Time::Now() - TimeDelta::FromSeconds(elapsed_seconds);
  prefs->SetInt64(kPrefsLastActivePingDay, daystart.ToInternalValue());
  prefs->SetInt64(kPrefsLastRollCallPingDay, daystart.ToInternalValue());
  return true;
}
}  // namespace {}

bool OmahaRequestAction::ParseResponse(xmlDoc* doc,
                                       OmahaResponse* output_object,
                                       ScopedActionCompleter* completer) {
  static const char* kUpdatecheckNodeXpath("/response/app/updatecheck");

  scoped_ptr_malloc<xmlXPathObject, ScopedPtrXmlXPathObjectFree>
      xpath_nodeset(GetNodeSet(doc, ConstXMLStr(kUpdatecheckNodeXpath)));
  if (!xpath_nodeset.get()) {
    completer->set_code(kErrorCodeOmahaResponseInvalid);
    return false;
  }

  xmlNodeSet* nodeset = xpath_nodeset->nodesetval;
  CHECK(nodeset) << "XPath missing UpdateCheck NodeSet";
  CHECK_GE(nodeset->nodeNr, 1);
  xmlNode* update_check_node = nodeset->nodeTab[0];

  // chromium-os:37289: The PollInterval is not supported by Omaha server
  // currently.  But still keeping this existing code in case we ever decide to
  // slow down the request rate from the server-side. Note that the
  // PollInterval is not persisted, so it has to be sent by the server on every
  // response to guarantee that the UpdateCheckScheduler uses this value
  // (otherwise, if the device got rebooted after the last server-indicated
  // value, it'll revert to the default value). Also kDefaultMaxUpdateChecks
  // value for the scattering logic is based on the assumption that we perform
  // an update check every hour so that the max value of 8 will roughly be
  // equivalent to one work day. If we decide to use PollInterval permanently,
  // we should update the max_update_checks_allowed to take PollInterval into
  // account.  Note: The parsing for PollInterval happens even before parsing
  // of the status because we may want to specify the PollInterval even when
  // there's no update.
  base::StringToInt(XmlGetProperty(update_check_node, "PollInterval"),
                    &output_object->poll_interval);

  // Check for the "elapsed_days" attribute in the "daystart"
  // element. This is the number of days since Jan 1 2007, 0:00
  // PST. If we don't have a persisted value of the Omaha InstallDate,
  // we'll use it to calculate it and then persist it.
  if (ParseInstallDate(doc, output_object) && !HasInstallDate(system_state_)) {
    // Since output_object->install_date_days is never negative, the
    // elapsed_days -> install-date calculation is reduced to simply
    // rounding down to the nearest number divisible by 7.
    int remainder = output_object->install_date_days % 7;
    int install_date_days_rounded =
        output_object->install_date_days - remainder;
    if (PersistInstallDate(system_state_,
                           install_date_days_rounded,
                           kProvisionedFromOmahaResponse)) {
      LOG(INFO) << "Set the Omaha InstallDate from Omaha Response to "
                << install_date_days_rounded << " days";
    }
  }

  if (!ParseStatus(update_check_node, output_object, completer))
    return false;

  // Note: ParseUrls MUST be called before ParsePackage as ParsePackage
  // appends the package name to the URLs populated in this method.
  if (!ParseUrls(doc, output_object, completer))
    return false;

  if (!ParsePackage(doc, output_object, completer))
    return false;

  if (!ParseParams(doc, output_object, completer))
    return false;

  return true;
}

bool OmahaRequestAction::ParseStatus(xmlNode* update_check_node,
                                     OmahaResponse* output_object,
                                     ScopedActionCompleter* completer) {
  // Get status.
  if (!xmlHasProp(update_check_node, ConstXMLStr("status"))) {
    LOG(ERROR) << "Omaha Response missing status";
    completer->set_code(kErrorCodeOmahaResponseInvalid);
    return false;
  }

  const string status(XmlGetProperty(update_check_node, "status"));
  if (status == "noupdate") {
    LOG(INFO) << "No update.";
    output_object->update_exists = false;
    SetOutputObject(*output_object);
    completer->set_code(kErrorCodeSuccess);
    return false;
  }

  if (status != "ok") {
    LOG(ERROR) << "Unknown Omaha response status: " << status;
    completer->set_code(kErrorCodeOmahaResponseInvalid);
    return false;
  }

  return true;
}

bool OmahaRequestAction::ParseUrls(xmlDoc* doc,
                                   OmahaResponse* output_object,
                                   ScopedActionCompleter* completer) {
  // Get the update URL.
  static const char* kUpdateUrlNodeXPath("/response/app/updatecheck/urls/url");

  scoped_ptr_malloc<xmlXPathObject, ScopedPtrXmlXPathObjectFree>
      xpath_nodeset(GetNodeSet(doc, ConstXMLStr(kUpdateUrlNodeXPath)));
  if (!xpath_nodeset.get()) {
    completer->set_code(kErrorCodeOmahaResponseInvalid);
    return false;
  }

  xmlNodeSet* nodeset = xpath_nodeset->nodesetval;
  CHECK(nodeset) << "XPath missing " << kUpdateUrlNodeXPath;
  CHECK_GE(nodeset->nodeNr, 1);

  LOG(INFO) << "Found " << nodeset->nodeNr << " url(s)";
  output_object->payload_urls.clear();
  for (int i = 0; i < nodeset->nodeNr; i++) {
    xmlNode* url_node = nodeset->nodeTab[i];
    const string codebase(XmlGetProperty(url_node, "codebase"));
    if (codebase.empty()) {
      LOG(ERROR) << "Omaha Response URL has empty codebase";
      completer->set_code(kErrorCodeOmahaResponseInvalid);
      return false;
    }
    output_object->payload_urls.push_back(codebase);
  }

  return true;
}

bool OmahaRequestAction::ParsePackage(xmlDoc* doc,
                                      OmahaResponse* output_object,
                                      ScopedActionCompleter* completer) {
  // Get the package node.
  static const char* kPackageNodeXPath(
      "/response/app/updatecheck/manifest/packages/package");

  scoped_ptr_malloc<xmlXPathObject, ScopedPtrXmlXPathObjectFree>
      xpath_nodeset(GetNodeSet(doc, ConstXMLStr(kPackageNodeXPath)));
  if (!xpath_nodeset.get()) {
    completer->set_code(kErrorCodeOmahaResponseInvalid);
    return false;
  }

  xmlNodeSet* nodeset = xpath_nodeset->nodesetval;
  CHECK(nodeset) << "XPath missing " << kPackageNodeXPath;
  CHECK_GE(nodeset->nodeNr, 1);

  // We only care about the first package.
  LOG(INFO) << "Processing first of " << nodeset->nodeNr << " package(s)";
  xmlNode* package_node = nodeset->nodeTab[0];

  // Get package properties one by one.

  // Parse the payload name to be appended to the base Url value.
  const string package_name(XmlGetProperty(package_node, "name"));
  LOG(INFO) << "Omaha Response package name = " << package_name;
  if (package_name.empty()) {
    LOG(ERROR) << "Omaha Response has empty package name";
    completer->set_code(kErrorCodeOmahaResponseInvalid);
    return false;
  }

  // Append the package name to each URL in our list so that we don't
  // propagate the urlBase vs packageName distinctions beyond this point.
  // From now on, we only need to use payload_urls.
  for (size_t i = 0; i < output_object->payload_urls.size(); i++)
    output_object->payload_urls[i] += package_name;

  // Parse the payload size.
  off_t size = ParseInt(XmlGetProperty(package_node, "size"));
  if (size <= 0) {
    LOG(ERROR) << "Omaha Response has invalid payload size: " << size;
    completer->set_code(kErrorCodeOmahaResponseInvalid);
    return false;
  }
  output_object->size = size;

  LOG(INFO) << "Payload size = " << output_object->size << " bytes";

  return true;
}

bool OmahaRequestAction::ParseParams(xmlDoc* doc,
                                     OmahaResponse* output_object,
                                     ScopedActionCompleter* completer) {
  // XPath location for response elements we care about.
  static const char* kManifestNodeXPath("/response/app/updatecheck/manifest");\
  static const char* kActionNodeXPath(
        "/response/app/updatecheck/manifest/actions/action");

  // Get the manifest node where version is present.
  scoped_ptr_malloc<xmlXPathObject, ScopedPtrXmlXPathObjectFree>
      xpath_manifest_nodeset(GetNodeSet(doc, ConstXMLStr(kManifestNodeXPath)));
  if (!xpath_manifest_nodeset.get()) {
    completer->set_code(kErrorCodeOmahaResponseInvalid);
    return false;
  }

  // Grab the only matching node there should be from the xpath.
  xmlNodeSet* nodeset = xpath_manifest_nodeset->nodesetval;
  CHECK(nodeset) << "XPath missing " << kManifestNodeXPath;
  CHECK_GE(nodeset->nodeNr, 1);
  xmlNode* manifest_node = nodeset->nodeTab[0];

  // Set the version.
  output_object->version = XmlGetProperty(manifest_node, kTagVersion);
  if (output_object->version.empty()) {
    LOG(ERROR) << "Omaha Response does not have version in manifest!";
    completer->set_code(kErrorCodeOmahaResponseInvalid);
    return false;
  }

  LOG(INFO) << "Received omaha response to update to version "
            << output_object->version;

  // Grab the action nodes.
  scoped_ptr_malloc<xmlXPathObject, ScopedPtrXmlXPathObjectFree>
      xpath_action_nodeset(GetNodeSet(doc, ConstXMLStr(kActionNodeXPath)));
  if (!xpath_action_nodeset.get()) {
    completer->set_code(kErrorCodeOmahaResponseInvalid);
    return false;
  }

  // We only care about the action that has event "postinstall", because this is
  // where Omaha puts all the generic name/value pairs in the rule.
  nodeset = xpath_action_nodeset->nodesetval;
  CHECK(nodeset) << "XPath missing " << kActionNodeXPath;
  LOG(INFO) << "Found " << nodeset->nodeNr
            << " action(s). Processing the postinstall action.";

  // pie_action_node holds the action node corresponding to the
  // postinstall event action, if present.
  xmlNode* pie_action_node = NULL;
  for (int i = 0; i < nodeset->nodeNr; i++) {
    xmlNode* action_node = nodeset->nodeTab[i];
    if (XmlGetProperty(action_node, "event") == "postinstall") {
      pie_action_node = action_node;
      break;
    }
  }

  if (!pie_action_node) {
    LOG(ERROR) << "Omaha Response has no postinstall event action";
    completer->set_code(kErrorCodeOmahaResponseInvalid);
    return false;
  }

  output_object->hash = XmlGetProperty(pie_action_node, kTagSha256);
  if (output_object->hash.empty()) {
    LOG(ERROR) << "Omaha Response has empty sha256 value";
    completer->set_code(kErrorCodeOmahaResponseInvalid);
    return false;
  }

  // Get the optional properties one by one.
  output_object->more_info_url = XmlGetProperty(pie_action_node, kTagMoreInfo);
  output_object->metadata_size =
      ParseInt(XmlGetProperty(pie_action_node, kTagMetadataSize));
  output_object->metadata_signature =
      XmlGetProperty(pie_action_node, kTagMetadataSignatureRsa);
  output_object->prompt = XmlGetProperty(pie_action_node, kTagPrompt) == "true";
  output_object->deadline = XmlGetProperty(pie_action_node, kTagDeadline);
  output_object->max_days_to_scatter =
      ParseInt(XmlGetProperty(pie_action_node, kTagMaxDaysToScatter));
  output_object->disable_p2p_for_downloading =
      (XmlGetProperty(pie_action_node, kTagDisableP2PForDownloading) == "true");
  output_object->disable_p2p_for_sharing =
      (XmlGetProperty(pie_action_node, kTagDisableP2PForSharing) == "true");
  output_object->public_key_rsa =
      XmlGetProperty(pie_action_node, kTagPublicKeyRsa);

  string max = XmlGetProperty(pie_action_node, kTagMaxFailureCountPerUrl);
  if (!base::StringToUint(max, &output_object->max_failure_count_per_url))
    output_object->max_failure_count_per_url = kDefaultMaxFailureCountPerUrl;

  output_object->is_delta_payload =
      XmlGetProperty(pie_action_node, kTagIsDeltaPayload) == "true";

  output_object->disable_payload_backoff =
      XmlGetProperty(pie_action_node, kTagDisablePayloadBackoff) == "true";

  return true;
}

// If the transfer was successful, this uses libxml2 to parse the response
// and fill in the appropriate fields of the output object. Also, notifies
// the processor that we're done.
void OmahaRequestAction::TransferComplete(HttpFetcher *fetcher,
                                          bool successful) {
  ScopedActionCompleter completer(processor_, this);
  string current_response(response_buffer_.begin(), response_buffer_.end());
  LOG(INFO) << "Omaha request response: " << current_response;

  // Events are best effort transactions -- assume they always succeed.
  if (IsEvent()) {
    CHECK(!HasOutputPipe()) << "No output pipe allowed for event requests.";
    if (event_->result == OmahaEvent::kResultError && successful &&
        system_state_->hardware()->IsOfficialBuild()) {
      LOG(INFO) << "Signalling Crash Reporter.";
      utils::ScheduleCrashReporterUpload();
    }
    completer.set_code(kErrorCodeSuccess);
    return;
  }

  if (!successful) {
    LOG(ERROR) << "Omaha request network transfer failed.";
    int code = GetHTTPResponseCode();
    // Makes sure we send sane error values.
    if (code < 0 || code >= 1000) {
      code = 999;
    }
    completer.set_code(static_cast<ErrorCode>(
        kErrorCodeOmahaRequestHTTPResponseBase + code));
    return;
  }

  // parse our response and fill the fields in the output object
  scoped_ptr_malloc<xmlDoc, ScopedPtrXmlDocFree> doc(
      xmlParseMemory(&response_buffer_[0], response_buffer_.size()));
  if (!doc.get()) {
    LOG(ERROR) << "Omaha response not valid XML";
    completer.set_code(response_buffer_.empty() ?
                       kErrorCodeOmahaRequestEmptyResponseError :
                       kErrorCodeOmahaRequestXMLParseError);
    return;
  }

  // If a ping was sent, update the last ping day preferences based on
  // the server daystart response.
  if (ShouldPing(ping_active_days_) ||
      ShouldPing(ping_roll_call_days_) ||
      ping_active_days_ == kPingTimeJump ||
      ping_roll_call_days_ == kPingTimeJump) {
    LOG_IF(ERROR, !UpdateLastPingDays(doc.get(), system_state_->prefs()))
        << "Failed to update the last ping day preferences!";
  }

  if (!HasOutputPipe()) {
    // Just set success to whether or not the http transfer succeeded,
    // which must be true at this point in the code.
    completer.set_code(kErrorCodeSuccess);
    return;
  }

  OmahaResponse output_object;
  if (!ParseResponse(doc.get(), &output_object, &completer))
    return;
  output_object.update_exists = true;
  SetOutputObject(output_object);

  if (params_->update_disabled()) {
    LOG(INFO) << "Ignoring Omaha updates as updates are disabled by policy.";
    output_object.update_exists = false;
    completer.set_code(kErrorCodeOmahaUpdateIgnoredPerPolicy);
    // Note: We could technically delete the UpdateFirstSeenAt state here.
    // If we do, it'll mean a device has to restart the UpdateFirstSeenAt
    // and thus help scattering take effect when the AU is turned on again.
    // On the other hand, it also increases the chance of update starvation if
    // an admin turns AU on/off more frequently. We choose to err on the side
    // of preventing starvation at the cost of not applying scattering in
    // those cases.
    return;
  }

  // If Omaha says to disable p2p, respect that
  if (output_object.disable_p2p_for_downloading) {
    LOG(INFO) << "Forcibly disabling use of p2p for downloading as "
              << "requested by Omaha.";
    params_->set_use_p2p_for_downloading(false);
  }
  if (output_object.disable_p2p_for_sharing) {
    LOG(INFO) << "Forcibly disabling use of p2p for sharing as "
              << "requested by Omaha.";
    params_->set_use_p2p_for_sharing(false);
  }

  // Update the payload state with the current response. The payload state
  // will automatically reset all stale state if this response is different
  // from what's stored already. We are updating the payload state as late
  // as possible in this method so that if a new release gets pushed and then
  // got pulled back due to some issues, we don't want to clear our internal
  // state unnecessarily.
  PayloadStateInterface* payload_state = system_state_->payload_state();
  payload_state->SetResponse(output_object);

  // It could be we've already exceeded the deadline for when p2p is
  // allowed or that we've tried too many times with p2p. Check that.
  if (params_->use_p2p_for_downloading()) {
    payload_state->P2PNewAttempt();
    if (!payload_state->P2PAttemptAllowed()) {
      LOG(INFO) << "Forcibly disabling use of p2p for downloading because "
                << "of previous failures when using p2p.";
      params_->set_use_p2p_for_downloading(false);
    }
  }

  // From here on, we'll complete stuff in CompleteProcessing() so
  // disable |completer| since we'll create a new one in that
  // function.
  completer.set_should_complete(false);

  // If we're allowed to use p2p for downloading we do not pay
  // attention to wall-clock-based waiting if the URL is indeed
  // available via p2p. Therefore, check if the file is available via
  // p2p before deferring...
  if (params_->use_p2p_for_downloading()) {
    LookupPayloadViaP2P(output_object);
  } else {
    CompleteProcessing();
  }
}

void OmahaRequestAction::CompleteProcessing() {
  ScopedActionCompleter completer(processor_, this);
  OmahaResponse& output_object = const_cast<OmahaResponse&>(GetOutputObject());
  PayloadStateInterface* payload_state = system_state_->payload_state();

  if (ShouldDeferDownload(&output_object)) {
    output_object.update_exists = false;
    LOG(INFO) << "Ignoring Omaha updates as updates are deferred by policy.";
    completer.set_code(kErrorCodeOmahaUpdateDeferredPerPolicy);
    return;
  }

  if (payload_state->ShouldBackoffDownload()) {
    output_object.update_exists = false;
    LOG(INFO) << "Ignoring Omaha updates in order to backoff our retry "
              << "attempts";
    completer.set_code(kErrorCodeOmahaUpdateDeferredForBackoff);
    return;
  }
  completer.set_code(kErrorCodeSuccess);
}

void OmahaRequestAction::OnLookupPayloadViaP2PCompleted(const string& url) {
  LOG(INFO) << "Lookup complete, p2p-client returned URL '" << url << "'";
  if (!url.empty()) {
    params_->set_p2p_url(url);
  } else {
    LOG(INFO) << "Forcibly disabling use of p2p for downloading "
              << "because no suitable peer could be found.";
    params_->set_use_p2p_for_downloading(false);
  }
  CompleteProcessing();
}

void OmahaRequestAction::LookupPayloadViaP2P(const OmahaResponse& response) {
  // If the device is in the middle of an update, the state variables
  // kPrefsUpdateStateNextDataOffset, kPrefsUpdateStateNextDataLength
  // tracks the offset and length of the operation currently in
  // progress. The offset is based from the end of the manifest which
  // is kPrefsManifestMetadataSize bytes long.
  //
  // To make forward progress and avoid deadlocks, we need to find a
  // peer that has at least the entire operation we're currently
  // working on. Otherwise we may end up in a situation where two
  // devices bounce back and forth downloading from each other,
  // neither making any forward progress until one of them decides to
  // stop using p2p (via kMaxP2PAttempts and kMaxP2PAttemptTimeSeconds
  // safe-guards). See http://crbug.com/297170 for an example)
  size_t minimum_size = 0;
  int64_t manifest_metadata_size = 0;
  int64_t next_data_offset = 0;
  int64_t next_data_length = 0;
  if (system_state_ != NULL &&
      system_state_->prefs()->GetInt64(kPrefsManifestMetadataSize,
                                       &manifest_metadata_size) &&
      manifest_metadata_size != -1 &&
      system_state_->prefs()->GetInt64(kPrefsUpdateStateNextDataOffset,
                                       &next_data_offset) &&
      next_data_offset != -1 &&
      system_state_->prefs()->GetInt64(kPrefsUpdateStateNextDataLength,
                                       &next_data_length)) {
    minimum_size = manifest_metadata_size + next_data_offset + next_data_length;
  }

  string file_id = utils::CalculateP2PFileId(response.hash, response.size);
  if (system_state_->p2p_manager() != NULL) {
    LOG(INFO) << "Checking if payload is available via p2p, file_id="
              << file_id << " minimum_size=" << minimum_size;
    system_state_->p2p_manager()->LookupUrlForFile(
        file_id,
        minimum_size,
        TimeDelta::FromSeconds(kMaxP2PNetworkWaitTimeSeconds),
        base::Bind(&OmahaRequestAction::OnLookupPayloadViaP2PCompleted,
                   base::Unretained(this)));
  }
}

bool OmahaRequestAction::ShouldDeferDownload(OmahaResponse* output_object) {
  if (params_->interactive()) {
    LOG(INFO) << "Not deferring download because update is interactive.";
    return false;
  }

  // If we're using p2p to download _and_ we have a p2p URL, we never
  // defer the download. This is because the download will always
  // happen from a peer on the LAN and we've been waiting in line for
  // our turn.
  if (params_->use_p2p_for_downloading() && !params_->p2p_url().empty()) {
    LOG(INFO) << "Download not deferred because download "
              << "will happen from a local peer (via p2p).";
    return false;
  }

  // We should defer the downloads only if we've first satisfied the
  // wall-clock-based-waiting period and then the update-check-based waiting
  // period, if required.
  if (!params_->wall_clock_based_wait_enabled()) {
    LOG(INFO) << "Wall-clock-based waiting period is not enabled,"
              << " so no deferring needed.";
    return false;
  }

  switch (IsWallClockBasedWaitingSatisfied(output_object)) {
    case kWallClockWaitNotSatisfied:
      // We haven't even satisfied the first condition, passing the
      // wall-clock-based waiting period, so we should defer the downloads
      // until that happens.
      LOG(INFO) << "wall-clock-based-wait not satisfied.";
      return true;

    case kWallClockWaitDoneButUpdateCheckWaitRequired:
      LOG(INFO) << "wall-clock-based-wait satisfied and "
                << "update-check-based-wait required.";
      return !IsUpdateCheckCountBasedWaitingSatisfied();

    case kWallClockWaitDoneAndUpdateCheckWaitNotRequired:
      // Wall-clock-based waiting period is satisfied, and it's determined
      // that we do not need the update-check-based wait. so no need to
      // defer downloads.
      LOG(INFO) << "wall-clock-based-wait satisfied and "
                << "update-check-based-wait is not required.";
      return false;

    default:
      // Returning false for this default case so we err on the
      // side of downloading updates than deferring in case of any bugs.
      NOTREACHED();
      return false;
  }
}

OmahaRequestAction::WallClockWaitResult
OmahaRequestAction::IsWallClockBasedWaitingSatisfied(
    OmahaResponse* output_object) {
  Time update_first_seen_at;
  int64 update_first_seen_at_int;

  if (system_state_->prefs()->Exists(kPrefsUpdateFirstSeenAt)) {
    if (system_state_->prefs()->GetInt64(kPrefsUpdateFirstSeenAt,
                                         &update_first_seen_at_int)) {
      // Note: This timestamp could be that of ANY update we saw in the past
      // (not necessarily this particular update we're considering to apply)
      // but never got to apply because of some reason (e.g. stop AU policy,
      // updates being pulled out from Omaha, changes in target version prefix,
      // new update being rolled out, etc.). But for the purposes of scattering
      // it doesn't matter which update the timestamp corresponds to. i.e.
      // the clock starts ticking the first time we see an update and we're
      // ready to apply when the random wait period is satisfied relative to
      // that first seen timestamp.
      update_first_seen_at = Time::FromInternalValue(update_first_seen_at_int);
      LOG(INFO) << "Using persisted value of UpdateFirstSeenAt: "
                << utils::ToString(update_first_seen_at);
    } else {
      // This seems like an unexpected error where the persisted value exists
      // but it's not readable for some reason. Just skip scattering in this
      // case to be safe.
     LOG(INFO) << "Not scattering as UpdateFirstSeenAt value cannot be read";
     return kWallClockWaitDoneAndUpdateCheckWaitNotRequired;
    }
  } else {
    update_first_seen_at = Time::Now();
    update_first_seen_at_int = update_first_seen_at.ToInternalValue();
    if (system_state_->prefs()->SetInt64(kPrefsUpdateFirstSeenAt,
                                         update_first_seen_at_int)) {
      LOG(INFO) << "Persisted the new value for UpdateFirstSeenAt: "
                << utils::ToString(update_first_seen_at);
    }
    else {
      // This seems like an unexpected error where the value cannot be
      // persisted for some reason. Just skip scattering in this
      // case to be safe.
      LOG(INFO) << "Not scattering as UpdateFirstSeenAt value "
                << utils::ToString(update_first_seen_at)
                << " cannot be persisted";
     return kWallClockWaitDoneAndUpdateCheckWaitNotRequired;
    }
  }

  TimeDelta elapsed_time = Time::Now() - update_first_seen_at;
  TimeDelta max_scatter_period = TimeDelta::FromDays(
      output_object->max_days_to_scatter);

  LOG(INFO) << "Waiting Period = "
            << utils::FormatSecs(params_->waiting_period().InSeconds())
            << ", Time Elapsed = "
            << utils::FormatSecs(elapsed_time.InSeconds())
            << ", MaxDaysToScatter = "
            << max_scatter_period.InDays();

  if (!output_object->deadline.empty()) {
    // The deadline is set for all rules which serve a delta update from a
    // previous FSI, which means this update will be applied mostly in OOBE
    // cases. For these cases, we shouldn't scatter so as to finish the OOBE
    // quickly.
    LOG(INFO) << "Not scattering as deadline flag is set";
    return kWallClockWaitDoneAndUpdateCheckWaitNotRequired;
  }

  if (max_scatter_period.InDays() == 0) {
    // This means the Omaha rule creator decides that this rule
    // should not be scattered irrespective of the policy.
    LOG(INFO) << "Not scattering as MaxDaysToScatter in rule is 0.";
    return kWallClockWaitDoneAndUpdateCheckWaitNotRequired;
  }

  if (elapsed_time > max_scatter_period) {
    // This means we've waited more than the upperbound wait in the rule
    // from the time we first saw a valid update available to us.
    // This will prevent update starvation.
    LOG(INFO) << "Not scattering as we're past the MaxDaysToScatter limit.";
    return kWallClockWaitDoneAndUpdateCheckWaitNotRequired;
  }

  // This means we are required to participate in scattering.
  // See if our turn has arrived now.
  TimeDelta remaining_wait_time = params_->waiting_period() - elapsed_time;
  if (remaining_wait_time.InSeconds() <= 0) {
    // Yes, it's our turn now.
    LOG(INFO) << "Successfully passed the wall-clock-based-wait.";

    // But we can't download until the update-check-count-based wait is also
    // satisfied, so mark it as required now if update checks are enabled.
    return params_->update_check_count_wait_enabled() ?
              kWallClockWaitDoneButUpdateCheckWaitRequired :
              kWallClockWaitDoneAndUpdateCheckWaitNotRequired;
  }

  // Not our turn yet, so we have to wait until our turn to
  // help scatter the downloads across all clients of the enterprise.
  LOG(INFO) << "Update deferred for another "
            << utils::FormatSecs(remaining_wait_time.InSeconds())
            << " per policy.";
  return kWallClockWaitNotSatisfied;
}

bool OmahaRequestAction::IsUpdateCheckCountBasedWaitingSatisfied() {
  int64 update_check_count_value;

  if (system_state_->prefs()->Exists(kPrefsUpdateCheckCount)) {
    if (!system_state_->prefs()->GetInt64(kPrefsUpdateCheckCount,
                                          &update_check_count_value)) {
      // We are unable to read the update check count from file for some reason.
      // So let's proceed anyway so as to not stall the update.
      LOG(ERROR) << "Unable to read update check count. "
                 << "Skipping update-check-count-based-wait.";
      return true;
    }
  } else {
    // This file does not exist. This means we haven't started our update
    // check count down yet, so this is the right time to start the count down.
    update_check_count_value = base::RandInt(
      params_->min_update_checks_needed(),
      params_->max_update_checks_allowed());

    LOG(INFO) << "Randomly picked update check count value = "
              << update_check_count_value;

    // Write out the initial value of update_check_count_value.
    if (!system_state_->prefs()->SetInt64(kPrefsUpdateCheckCount,
                                          update_check_count_value)) {
      // We weren't able to write the update check count file for some reason.
      // So let's proceed anyway so as to not stall the update.
      LOG(ERROR) << "Unable to write update check count. "
                 << "Skipping update-check-count-based-wait.";
      return true;
    }
  }

  if (update_check_count_value == 0) {
    LOG(INFO) << "Successfully passed the update-check-based-wait.";
    return true;
  }

  if (update_check_count_value < 0 ||
      update_check_count_value > params_->max_update_checks_allowed()) {
    // We err on the side of skipping scattering logic instead of stalling
    // a machine from receiving any updates in case of any unexpected state.
    LOG(ERROR) << "Invalid value for update check count detected. "
               << "Skipping update-check-count-based-wait.";
    return true;
  }

  // Legal value, we need to wait for more update checks to happen
  // until this becomes 0.
  LOG(INFO) << "Deferring Omaha updates for another "
            << update_check_count_value
            << " update checks per policy";
  return false;
}

// static
bool OmahaRequestAction::ParseInstallDate(xmlDoc* doc,
                                          OmahaResponse* output_object) {
  scoped_ptr_malloc<xmlXPathObject, ScopedPtrXmlXPathObjectFree>
      xpath_nodeset(GetNodeSet(doc, ConstXMLStr("/response/daystart")));

  if (xpath_nodeset.get() == NULL)
    return false;

  xmlNodeSet* nodeset = xpath_nodeset->nodesetval;
  if (nodeset == NULL || nodeset->nodeNr < 1)
    return false;

  xmlNode* daystart_node = nodeset->nodeTab[0];
  if (!xmlHasProp(daystart_node, ConstXMLStr("elapsed_days")))
    return false;

  int64_t elapsed_days = 0;
  if (!base::StringToInt64(XmlGetProperty(daystart_node, "elapsed_days"),
                           &elapsed_days))
    return false;

  if (elapsed_days < 0)
    return false;

  output_object->install_date_days = elapsed_days;
  return true;
}

// static
bool OmahaRequestAction::HasInstallDate(SystemState *system_state) {
  PrefsInterface* prefs = system_state->prefs();
  if (prefs == NULL)
    return false;

  return prefs->Exists(kPrefsInstallDateDays);
}

// static
bool OmahaRequestAction::PersistInstallDate(
    SystemState *system_state,
    int install_date_days,
    InstallDateProvisioningSource source) {
  TEST_AND_RETURN_FALSE(install_date_days >= 0);

  PrefsInterface* prefs = system_state->prefs();
  if (prefs == NULL)
    return false;

  if (!prefs->SetInt64(kPrefsInstallDateDays, install_date_days))
    return false;

  string metric_name = "Installer.InstallDateProvisioningSource";
  system_state->metrics_lib()->SendEnumToUMA(
      metric_name,
      static_cast<int>(source),  // Sample.
      kProvisionedMax);          // Maximum.

  metric_name = metrics::kMetricInstallDateProvisioningSource;
  system_state->metrics_lib()->SendEnumToUMA(
      metric_name,
      static_cast<int>(source),  // Sample.
      kProvisionedMax);          // Maximum.

  return true;
}

void OmahaRequestAction::ActionCompleted(ErrorCode code) {
  // We only want to report this on "update check".
  if (ping_only_ || event_ != nullptr)
    return;

  metrics::CheckResult result = metrics::CheckResult::kUnset;
  metrics::CheckReaction reaction = metrics::CheckReaction::kUnset;
  metrics::DownloadErrorCode download_error_code =
      metrics::DownloadErrorCode::kUnset;

  // Regular update attempt.
  switch (code) {
  case kErrorCodeSuccess:
    // OK, we parsed the response successfully but that does
    // necessarily mean that an update is available.
    if (HasOutputPipe()) {
      const OmahaResponse& response = GetOutputObject();
      if (response.update_exists) {
        result = metrics::CheckResult::kUpdateAvailable;
        reaction = metrics::CheckReaction::kUpdating;
      } else {
        result = metrics::CheckResult::kNoUpdateAvailable;
      }
    } else {
      result = metrics::CheckResult::kNoUpdateAvailable;
    }
    break;

  case kErrorCodeOmahaUpdateIgnoredPerPolicy:
    result = metrics::CheckResult::kUpdateAvailable;
    reaction = metrics::CheckReaction::kIgnored;
    break;

  case kErrorCodeOmahaUpdateDeferredPerPolicy:
    result = metrics::CheckResult::kUpdateAvailable;
    reaction = metrics::CheckReaction::kDeferring;
    break;

  case kErrorCodeOmahaUpdateDeferredForBackoff:
    result = metrics::CheckResult::kUpdateAvailable;
    reaction = metrics::CheckReaction::kBackingOff;
    break;

  default:
    // We report two flavors of errors, "Download errors" and "Parsing
    // error". Try to convert to the former and if that doesn't work
    // we know it's the latter.
    metrics::DownloadErrorCode tmp_error = utils::GetDownloadErrorCode(code);
    if (tmp_error != metrics::DownloadErrorCode::kInputMalformed) {
      result = metrics::CheckResult::kDownloadError;
      download_error_code = tmp_error;
    } else {
      result = metrics::CheckResult::kParsingError;
    }
    break;
  }

  metrics::ReportUpdateCheckMetrics(system_state_,
                                    result, reaction, download_error_code);
}

}  // namespace chromeos_update_engine
