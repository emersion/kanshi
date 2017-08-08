extern crate edid;
extern crate getopts;
#[macro_use]
extern crate nom;

mod backend;
mod store;
mod frontend;

use std::io::Write;
use std::env;

use getopts::Options;

use backend::{ConnectedOutput, Backend, SysFsBackend};
use store::{SavedOutput, Store, GnomeStore};
use frontend::{MatchedOutput, Frontend, SwayFrontend};

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
			let vendor = self.edid.header.vendor[..].iter().collect::<String>();
			if vendor != other.vendor {
				return false;
			}
		}
		if other.product.starts_with("0x") {
			let other_product = u16::from_str_radix(other.product.trim_left_matches("0x"), 16).unwrap();
			if other_product != self.edid.header.product {
				return false;
			}
		} else if other.product != "" {
			let ok = self.edid.descriptors.iter()
			.filter_map(|d| match d {
				&edid::Descriptor::ProductName(ref s) => Some(s.as_ref()),
				_ => None,
			})
			.nth(0)
			.map(|product| product == other.product)
			.unwrap_or(false);

			if !ok {
				return false;
			}
		}
		if other.serial.starts_with("0x") {
			let other_serial = u32::from_str_radix(other.serial.trim_left_matches("0x"), 16).unwrap();
			if other_serial != self.edid.header.serial {
				return false;
			}
		} else if other.serial != "" {
			let ok = self.edid.descriptors.iter()
			.filter_map(|d| match d {
				&edid::Descriptor::SerialNumber(ref s) => Some(s.as_ref()),
				_ => None,
			})
			.nth(0)
			.map(|serial| serial == other.serial)
			.unwrap_or(false);

			if !ok {
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
	.optopt("", "primary-workspace", "set the primary workspace name", "<workspace>")
	.optflag("h", "help", "print this help menu");

	let opts_matches = opts.parse(&args[1..]).unwrap();

	if opts_matches.opt_present("h") {
		print_usage(&program, opts);
		return;
	}

	let mut stderr = std::io::stdout();

	let backend = SysFsBackend{};
	let connected_outputs = match backend.list_outputs() {
		Ok(c) => c,
		Err(err) => {
			writeln!(&mut stderr, "Error: cannot list connected monitors: {}", err).unwrap();
			std::process::exit(1);
		},
	};

	writeln!(&mut stderr, "Connected outputs: {:?}", connected_outputs).unwrap();

	//let store = GnomeStore{};
	let store = store::KanshiStore{};
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
		let n_saved = config.len();
		if n_saved != connected_outputs.len() {
			return None;
		}

		let matched = config.into_iter()
		.filter_map(|saved| {
			connected_outputs.iter()
			.find(|connected| **connected == *saved)
			.map(|connected| MatchedOutput{connected, saved})
		})
		.collect::<Vec<_>>();

		if n_saved != matched.len() {
			return None;
		}

		Some(matched)
	})
	.nth(0);

	writeln!(&mut stderr, "Matching configuration: {:?}", &configuration).unwrap();

	let frontend = SwayFrontend::new(opts_matches);
	match frontend.apply_configuration(configuration) {
		Ok(()) => (),
		Err(err) => {
			writeln!(&mut stderr, "Error: cannot apply configuration: {}", err).unwrap();
			std::process::exit(1);
		},
	};
}
