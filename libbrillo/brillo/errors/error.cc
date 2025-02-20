// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/errors/error.h>

#include <cstdarg>
#include <string_view>
#include <utility>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

using brillo::Error;
using brillo::ErrorPtr;

namespace {
inline void LogError(const base::Location& location,
                     std::string_view domain,
                     std::string_view code,
                     std::string_view message) {
  if (!LOG_IS_ON(ERROR)) {
    return;
  }
  // Use logging::LogMessage() directly instead of LOG(ERROR) to substitute
  // the current error location with the location passed in to the Error object.
  // This way the log will contain the actual location of the error, and not
  // as if it always comes from brillo/errors/error.cc(22).
  logging::LogMessage(location.file_name() ? location.file_name() : "unknown",
                      location.line_number(), logging::LOGGING_ERROR)
          .stream()
      << (location.function_name() ? location.function_name() : "unknown")
      << "(...): " << "Domain=" << domain << ", Code=" << code
      << ", Message=" << message;
}
}  // anonymous namespace

ErrorPtr Error::Create(const base::Location& location,
                       std::string_view domain,
                       std::string_view code,
                       std::string_view message) {
  return Create(location, domain, code, message, ErrorPtr());
}

ErrorPtr Error::Create(const base::Location& location,
                       std::string_view domain,
                       std::string_view code,
                       std::string_view message,
                       ErrorPtr inner_error) {
  LogError(location, domain, code, message);
  return CreateNoLog(location, domain, code, message, std::move(inner_error));
}

ErrorPtr Error::CreateNoLog(const base::Location& location,
                            std::string_view domain,
                            std::string_view code,
                            std::string_view message,
                            ErrorPtr inner_error) {
  return ErrorPtr(
      new Error(location, domain, code, message, std::move(inner_error)));
}

void Error::AddTo(ErrorPtr* error,
                  const base::Location& location,
                  std::string_view domain,
                  std::string_view code,
                  std::string_view message) {
  if (error) {
    *error = Create(location, domain, code, message, std::move(*error));
  } else {
    // Create already logs the error, but if |error| is nullptr,
    // we still want to log the error...
    LogError(location, domain, code, message);
  }
}

void Error::AddToPrintf(ErrorPtr* error,
                        const base::Location& location,
                        std::string_view domain,
                        std::string_view code,
                        const char* format,
                        ...) {
  va_list ap;
  va_start(ap, format);
  std::string message = base::StringPrintV(format, ap);
  va_end(ap);
  AddTo(error, location, domain, code, message);
}

ErrorPtr Error::Clone() const {
  ErrorPtr inner_error = inner_error_ ? inner_error_->Clone() : nullptr;
  return ErrorPtr(
      new Error(location_, domain_, code_, message_, std::move(inner_error)));
}

bool Error::HasDomain(std::string_view domain) const {
  return FindErrorOfDomain(this, domain) != nullptr;
}

bool Error::HasError(std::string_view domain, std::string_view code) const {
  return FindError(this, domain, code) != nullptr;
}

const Error* Error::GetFirstError() const {
  const Error* err = this;
  while (err->GetInnerError()) {
    err = err->GetInnerError();
  }
  return err;
}

Error::Error(const base::Location& location,
             std::string_view domain,
             std::string_view code,
             std::string_view message,
             ErrorPtr inner_error)
    : domain_(domain),
      code_(code),
      message_(message),
      location_(location),
      inner_error_(std::move(inner_error)) {}

const Error* Error::FindErrorOfDomain(const Error* error_chain_start,
                                      std::string_view domain) {
  while (error_chain_start) {
    if (error_chain_start->GetDomain() == domain) {
      break;
    }
    error_chain_start = error_chain_start->GetInnerError();
  }
  return error_chain_start;
}

const Error* Error::FindError(const Error* error_chain_start,
                              std::string_view domain,
                              std::string_view code) {
  while (error_chain_start) {
    if (error_chain_start->GetDomain() == domain &&
        error_chain_start->GetCode() == code) {
      break;
    }
    error_chain_start = error_chain_start->GetInnerError();
  }
  return error_chain_start;
}
