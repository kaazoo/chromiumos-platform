//
// Copyright (C) 2012 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <cstdlib>
#include <string>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <chromeos/syslog_logging.h>
#include <dbus-c++/eventloop-integration.h>

#include "shill/rpc_task.h"
#include "shill/shims/environment.h"
#include "shill/shims/task_proxy.h"

using shill::shims::Environment;
using std::map;
using std::string;

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);
  chromeos::InitLog(chromeos::kLogToSyslog | chromeos::kLogHeader);

  Environment* environment = Environment::GetInstance();
  string service, path, reason;
  if (!environment->GetVariable(shill::kRPCTaskServiceVariable, &service) ||
      !environment->GetVariable(shill::kRPCTaskPathVariable, &path) ||
      !environment->GetVariable("script_type", &reason)) {
    LOG(ERROR) << "Environment variables not available.";
    return EXIT_FAILURE;
  }

  DBus::BusDispatcher dispatcher;
  DBus::default_dispatcher = &dispatcher;
  DBus::Connection connection(DBus::Connection::SystemBus());
  shill::shims::TaskProxy proxy(&connection, path, service);
  map<string, string> env = environment->AsMap();
  proxy.Notify(reason, env);
  return EXIT_SUCCESS;
}
