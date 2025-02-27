// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the command "dmesg" for crosh through debugd.

use std::collections::HashMap;
use std::fmt::Write as _;
use std::io::Write;

use dbus::arg::{self, Variant};
use getopts::Options;

use crate::debugd::Debugd;
use crate::dispatcher::{self, Arguments, Command, Dispatcher};

/* We keep a reduced set of options here for security */
const FLAGS: [(&str, &str, &str); 10] = [
    (
        "d",
        "show_delta",
        "Display the timestamp and the time delta
            spent between messages",
    ),
    ("H", "human", "Enable human-readable output."),
    ("k", "kernel", "Print kernel messages."),
    ("L", "color", "Colorize the output."),
    (
        "p",
        "force-prefix",
        "Add facility, level or timestamp
            information to each line of a multi-line message.",
    ),
    ("r", "raw", "Print the raw message buffer."),
    ("T", "ctime", "Print human-readable timestamps."),
    ("t", "notime", "Do not print kernel's timestamps."),
    ("u", "userspace", "Print userspace messages."),
    (
        "x",
        "decode",
        "Decode facility and level (priority) numbers
            to human-readable prefixes.",
    ),
];

pub fn register(dispatcher: &mut Dispatcher) {
    dispatcher.register_command(
        Command::new("dmesg", "", "Run the dmesg command via debugd.")
            .set_command_callback(Some(execute_dmesg))
            .set_help_callback(dmesg_help),
    );
}

fn dmesg_help(_cmd: &Command, w: &mut dyn Write, _level: usize) {
    let mut help = "Usage: dmesg [options]\n".to_string();
    for flag in FLAGS.iter() {
        let _ = write!(help, "\t-{}, --{}\n\t\t{}\n", flag.0, flag.1, flag.2);
    }
    w.write_all(help.as_bytes()).unwrap();
    w.flush().unwrap();
}

fn execute_dmesg(_cmd: &Command, args: &Arguments) -> Result<(), dispatcher::Error> {
    let mut opts = Options::new();

    for flag in FLAGS.iter() {
        opts.optflag(flag.0, flag.1, flag.2);
    }

    let matches = opts
        .parse(args.get_tokens())
        .map_err(|_| dispatcher::Error::CommandReturnedError)?;

    let mut dbus_options = HashMap::new();
    for flag in FLAGS.iter() {
        let name = flag.1;
        if matches.opt_present(name) {
            let val_true: Variant<Box<dyn arg::RefArg>> = Variant(Box::new(1));
            dbus_options.insert(name.to_string(), val_true);
        }
    }

    let connection = Debugd::new().map_err(|_| dispatcher::Error::CommandReturnedError)?;

    let output = connection.call_dmesg(dbus_options).map_err(|err| {
        println!("ERROR: Got unexpected result: {}", err);
        dispatcher::Error::CommandReturnedError
    })?;
    // Print the response.
    print!("{}", output);
    Ok(())
}
