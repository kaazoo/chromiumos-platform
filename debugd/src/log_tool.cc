// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log_tool.h"

#include <base/file_util.h>
#include <base/logging.h>
#include <base/string_split.h>

#include "process_with_output.h"

namespace debugd {

const char *kShell = "/bin/sh";

using std::string;
using std::vector;

typedef vector<string> Strings;

string Run(const string& cmdline) {
  string output;
  ProcessWithOutput p;
  string tailed_cmdline = cmdline + " | tail -c 32768";
  if (!p.Init())
    return "<not available>";
  p.AddArg(kShell);
  p.AddStringOption("-c", tailed_cmdline);
  if (p.Run())
    return "<not available>";
  p.GetOutput(&output);
  if (!output.size())
    return "<empty>";
  return output;
}

struct Log {
  const char *name;
  const char *command;
};

static struct Log logs[] = {
  { "CLIENT_ID", "/bin/cat '/home/chronos/Consent To Send Stats'" },
  { "LOGDATE", "/bin/date" },
  { "Xorg.0.log", "/bin/cat /var/log/Xorg.0.log" },
  { "alsa_controls", "/usr/bin/amixer -c0 contents" },
  { "bios_info", "/bin/cat /var/log/bios_info.txt" },
  { "board-specific",
        "/usr/share/userfeedback/scripts/get_board_specific_info" },
  { "boot_times", "/bin/cat /tmp/boot-times-sent" },
  { "chrome_log", "/bin/cat /home/chronos/user/log/chrome" },
  { "chrome_system_log", "/bin/cat /var/log/chrome/chrome" },
  { "cpu", "/usr/bin/uname -p" },
  { "cpuinfo", "/bin/cat /proc/cpuinfo" },
  { "dmesg", "/bin/dmesg" },
  { "ec_info", "/bin/cat /var/log/ec_info.txt" },
  { "font_info", "/usr/share/userfeedback/scripts/font_info" },
  { "hardware_class", "/usr/bin/crossystem hwid" },
  { "hostname", "/bin/hostname" },
  { "hw_platform", "/usr/bin/uname -i" },
  { "ifconfig", "/sbin/ifconfig -a" },
  { "login-times", "/bin/cat /home/chronos/user/login-times" },
  { "logout-times", "/bin/cat /home/chronos/user/logout-times" },
  { "lsmod", "lsmod" },
  { "lspci", "/usr/sbin/lspci" },
  { "lsusb", "lsusb" },
  { "meminfo", "cat /proc/meminfo" },
  { "memory_spd_info", "/bin/cat /var/log/memory_spd_info.txt" },
  { "mm-status", "/usr/share/userfeedback/scripts/mm-status" },
  { "network-devices", "/usr/bin/connectivity show devices" },
  { "network-services", "/usr/bin/connectivity show services" },
  { "power-supply-info", "/usr/bin/power-supply-info" },
  { "powerd.LATEST", "/bin/cat /var/log/power_manager/powerd.LATEST" },
  { "powerd.out", "/bin/cat /var/log/power_manager/powerd.out" },
  { "powerm.LATEST", "/bin/cat /var/log/power_manager/powerm.LATEST" },
  { "powerm.out", "/bin/cat /var/log/power_manager/powerm.out" },
  // Changed from 'ps ux' to 'ps aux' since we're running as debugd, not chronos
  { "ps", "/bin/ps aux" },
  { "syslog", "/usr/share/userfeedback/scripts/getmsgs --last '2 hours'"
              " /var/log/messages" },
  { "touchpad", "/opt/google/touchpad/tpcontrol status" },
  { "touchpad_activity", "/opt/google/touchpad/generate_userfeedback alt" },
  { "ui_log", "/usr/share/userfeedback/scripts/get_log /var/log/ui/ui.LATEST" },
  { "uname", "/bin/uname -a" },
  { "update_engine.log", "cat $(ls -1tr /var/log/update_engine | tail -5 | sed"
         " s.^./var/log/update_engine/.)" },
  { "verified boot", "/bin/cat /var/log/debug_vboot_noisy.log" },
  { "vpd_2.0", "/bin/cat /var/log/vpd_2.0.txt" },
  { "wifi_status", "/usr/bin/network_diagnostics --wifi --no-log" },

  // Stuff pulled out of the original list. These need access to the running X
  // session, which we'd rather not give to debugd, or return info specific to
  // the current session (in the setsid(2) sense), which is not useful for
  // debugd
  // { "env", "set" },
  // { "setxkbmap", "/usr/bin/setxkbmap -print -query" },
  // { "xrandr", "/usr/bin/xrandr --verbose" }
};

LogTool::LogTool() { }
LogTool::~LogTool() { }

string LogTool::GetLog(const string& name, DBus::Error& error) { // NOLINT
  for (unsigned int i = 0; i < arraysize(logs); i++)
    if (!strcmp(name.c_str(), logs[i].name))
      return Run(logs[i].command);
  return "<invalid log name>";
}

std::map<string, string> LogTool::GetAllLogs(DBus::Error& error) { // NOLINT
  std::map<string, string> result;
  for (unsigned int i = 0; i < arraysize(logs); i++)
    result[logs[i].name] = Run(logs[i].command);
  return result;
}

};  // namespace debugd
