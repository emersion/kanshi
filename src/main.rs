extern crate edid;
extern crate getopts;
#[macro_use]
extern crate nom;

mod backend;
mod store;
mod frontend;
mod notifier;

use std::io::Write;
use std::env;

use getopts::Options;

use backend::{ConnectedOutput, Backend, SysFsBackend};
use store::{SavedOutput, SavedConfiguration, Store, GnomeStore, KanshiStore};
use frontend::{MatchedOutput, Frontend, SwayFrontend};
use notifier::{Notifier, UdevNotifier};
use std::sync::mpsc::channel;
use std::process::Command;

#[derive(Debug)]
pub struct MatchedConfiguration<'a> {
	pub outputs: Vec<MatchedOutput<'a>>,
	pub saved: &'a SavedConfiguration,
}

fn connector_type(name: &str) -> Option<String> {
	let name = name.to_lowercase();

	[
		"VGA", "Unknown", "DVI", "Composite", "SVIDEO", "LVDS", "Component", "DIN", "DP", "HDMI",
		"TV", "eDP", "Virtual", "DSI",
	]
	.iter()
	.find(|t| name.starts_with(t.to_lowercase().as_str()))
	.map(|s| s.to_string())
}

impl PartialEq<SavedOutput> for ConnectedOutput {
	fn eq(&self, other: &SavedOutput) -> bool {
		if other.name != "" {
			if let Some(t) = connector_type(&self.name) {
				if let Some(other_t) = connector_type(&other.name) {
					if t != other_t {
						return false;
					}
				} else {
					return false;
				}
			}
		}
		if other.vendor != "" {
			if self.vendor() != other.vendor {
				return false;
			}
		}
		if other.product.starts_with("0x") {
			let other_product = u16::from_str_radix(other.product.trim_left_matches("0x"), 16).unwrap();
			if other_product != self.edid.header.product {
				return false;
			}
		} else if other.product != "" {
			if self.product() != other.product {
				return false;
			}
		}
		if other.serial.starts_with("0x") {
			let other_serial = u32::from_str_radix(other.serial.trim_left_matches("0x"), 16).unwrap();
			if other_serial != self.edid.header.serial {
				return false;
			}
		} else if other.serial != "" {
			if self.serial() != other.serial {
				return false;
			}
		}
		return true;
	}
}

fn print_usage(program: &str, opts: Options) {
	let brief = format!("Usage: {} [options]", program);
	print!("{}", opts.usage(&brief));
}

fn main() {
	let args: Vec<String> = env::args().collect();
	let program = args[0].clone();

	let mut opts = Options::new();

	opts
	.optopt("b", "backend", "set the backend (sysfs)", "<backend>")
	.optopt("s", "store", "set the store (gnome, kanshi)", "<store>")
	.optopt("f", "frontend", "set the frontend (sway)", "<frontend>")
	.optopt("n", "notifier", "set the notifier (udev)", "<notifier>")
	.optopt("", "primary-workspace", "set the primary workspace name (sway)", "<workspace>")
	.optflag("h", "help", "print this help menu");

	let opts_matches = opts.parse(&args[1..]).unwrap();

	if opts_matches.opt_present("h") {
		print_usage(&program, opts);
		return;
	}

	let mut stderr = std::io::stderr();

	let backend: Box<Backend> = match opts_matches.opt_str("backend").as_ref().map(String::as_ref) {
		None | Some("sysfs") => Box::new(SysFsBackend{}),
		_ => panic!("Unknown backend"),
	};

	let store: Box<Store> = match opts_matches.opt_str("store").as_ref().map(String::as_ref) {
		Some("gnome") => Box::new(GnomeStore{}),
		None | Some("kanshi") => Box::new(KanshiStore{}),
		_ => panic!("Unknown store"),
	};

	let notifier: Option<Box<Notifier>> = match opts_matches.opt_str("notifier").as_ref().map(String::as_ref) {
		None | Some("udev") => Some(Box::new(UdevNotifier{})),
		Some("none") => None,
		_ => panic!("Unknown notifier"),
	};

	let frontend: Box<Frontend> = match opts_matches.opt_str("frontend").as_ref().map(String::as_ref) {
		None | Some("sway") => Box::new(SwayFrontend::new(opts_matches)),
		_ => panic!("Unknown frontend"),
	};

	let rx = notifier.map(|notifier| {
		let (tx, rx) = channel();
		notifier.notify(tx).unwrap();
		rx
	});

	loop {
		let connected_outputs = match backend.list_outputs() {
			Ok(c) => c,
			Err(err) => {
				writeln!(&mut stderr, "Error: cannot list connected monitors: {}", err).unwrap();
				std::process::exit(1);
			},
		};

		writeln!(&mut stderr, "Connected outputs:").unwrap();
		for o in &connected_outputs {
			writeln!(&mut stderr, "{}", o).unwrap();
		}

		let configurations = match store.list_configurations() {
			Ok(c) => c,
			Err(err) => {
				writeln!(&mut stderr, "Error: cannot list saved monitor configurations: {}", err).unwrap();
				std::process::exit(1);
			},
		};

		//writeln!(&mut stderr, "Saved configurations: {:?}", configurations).unwrap();

		let connected_outputs = &connected_outputs;
		let configuration = configurations.iter()
		.filter_map(|config| {
			let n_saved = config.outputs.len();
			if n_saved != connected_outputs.len() {
				return None;
			}

			let matched = config.outputs.iter()
			.filter_map(|saved| {
				connected_outputs.iter()
				.find(|connected| **connected == *saved)
				.map(|connected| MatchedOutput{connected, saved})
			})
			.collect::<Vec<_>>();

			if n_saved != matched.len() {
				return None;
			}

			Some(MatchedConfiguration{outputs: matched, saved: config})
		})
		.nth(0);

		writeln!(&mut stderr, "Matching configuration: {:?}", &configuration).unwrap();

		{
			let outputs = configuration.as_ref().map(|config| config.outputs.as_ref());
			match frontend.apply_configuration(outputs) {
				Ok(()) => (),
				Err(err) => {
					writeln!(&mut stderr, "Error: cannot apply configuration: {}", err).unwrap();
					std::process::exit(1);
				},
			};
		}

		match configuration.map(|config| &config.saved.exec) {
			Some(exec) => {
				for e in exec {
					let cmd = e.get(0).unwrap();
					let args = e.get(1..).unwrap();
					writeln!(&mut stderr, "Executing command: {:?}", &cmd).unwrap();
					Command::new(cmd).args(args).spawn().unwrap();
				}
			},
			None => (),
		}

		match rx {
			Some(ref rx) => {
				writeln!(&mut stderr, "Waiting for output changes...").unwrap();
				rx.recv().unwrap();
			}
			None => break
		}
	}
}
