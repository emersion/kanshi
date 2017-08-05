extern crate edid;
extern crate getopts;
extern crate xmltree;

use std::env;
use std::fs;
use std::fs::File;
use std::io::prelude::*;
use std::io::Write;
use std::path::PathBuf;

use getopts::Options;
use xmltree::Element;

#[derive(Debug, Default)]
struct SavedOutput {
	name: String,
	vendor: String,
	product: String,
	serial: String,

	width: i32,
	height: i32,
	rate: f32,
	x: i32,
	y: i32,
	//rotation: Rotation,
	//reflect_x: bool,
	//reflect_y: bool,
	primary: bool,
	//presentation: bool,
	//underscanning: bool,
}

#[derive(Debug)]
struct ConnectedOutput {
	name: String,
	edid: edid::EDID,
}

impl PartialEq<SavedOutput> for ConnectedOutput {
	fn eq(&self, other: &SavedOutput) -> bool {
		// TODO: more permissive comparison, try to extract type and number
		//if self.name.replace("-", "") != other.name.replace("-", "") {
		//	return false;
		//}
		if let Some(t) = connector_type(&self.name) {
			if let Some(other_t) = connector_type(&other.name) {
				if t != other_t {
					return false;
				}
			} else {
				return false;
			}
		}
		if self.edid.header.vendor[..].iter().collect::<String>() != other.vendor {
			return false;
		}
		if other.product.starts_with("0x") {
			let other_product = u16::from_str_radix(other.product.trim_left_matches("0x"), 16).unwrap();
			if other_product != self.edid.header.product {
				return false;
			}
		} else {
			let ok = self.edid.descriptors.iter()
			.filter_map(|d| match d {
				&edid::Descriptor::Name(ref s) => Some(s.as_ref()),
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
		} else {
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

fn parse_bool(s: &str) -> bool {
	s == "yes"
}

fn connector_type(name: &str) -> Option<String> {
	let name = name.to_lowercase();

	[
		"VGA", "Unknown", "VDI", "Composite", "SVIDEO", "LVDS", "Component", "DIN", "DP", "HDMI",
		"TV", "eDP", "Virtual", "DSI",
	]
	.iter()
	.find(|t| name.starts_with(t.to_lowercase().as_str()))
	.map(|s| s.to_string())
}

fn print_usage(program: &str, opts: Options) {
	let brief = format!("Usage: {} [options]", program);
	print!("{}", opts.usage(&brief));
}

const OUTPUT_PREFIX: &str = "card0-";

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

	let connected_outputs = fs::read_dir("/sys/class/drm").unwrap()
	.map(|r| r.unwrap())
	.filter(|e| e.file_name().to_str().unwrap().starts_with(OUTPUT_PREFIX))
	.filter(|e| {
		let mut status = String::new();
		File::open(e.path().join("status")).unwrap().read_to_string(&mut status).unwrap();
		status.trim() == "connected"
	})
	.map(|e| {
		let name = e.file_name().to_str().unwrap().trim_left_matches(OUTPUT_PREFIX).to_string();

		let mut buf = Vec::new();
		File::open(e.path().join("edid")).unwrap().read_to_end(&mut buf).unwrap();
		let (_, edid) = edid::parse(&buf).unwrap();

		ConnectedOutput{name, edid}
	})
	.collect::<Vec<_>>();

	eprintln!("Connected outputs: {:?}", connected_outputs);

	let user_config_path = env::var("XDG_CONFIG_HOME")
	.map(PathBuf::from)
	.unwrap_or(env::home_dir().unwrap().join(".config"));
	let monitors_path = user_config_path.join("monitors.xml");

	let monitors = Element::parse(File::open(&monitors_path).unwrap()).unwrap();
	let configuration = monitors.children.iter()
	.filter(|e| e.name == "configuration")
	.map(|e| {
		e.children.iter()
		.filter(|e| e.name == "output")
		.map(|e| {
			let mut o = SavedOutput{
				name: e.attributes.get("name").unwrap().to_owned(),
				vendor: e.get_child("vendor").unwrap().text.as_ref().unwrap().to_owned(),
				product: e.get_child("product").unwrap().text.as_ref().unwrap().to_owned(),
				serial: e.get_child("serial").unwrap().text.as_ref().unwrap().to_owned(),
				..SavedOutput::default()
			};

			if let Some(c) = e.get_child("width") {
				o.width = c.text.as_ref().unwrap().parse::<i32>().unwrap()
			}
			if let Some(c) = e.get_child("height") {
				o.height = c.text.as_ref().unwrap().parse::<i32>().unwrap()
			}
			if let Some(c) = e.get_child("rate") {
				o.rate = c.text.as_ref().unwrap().parse::<f32>().unwrap()
			}
			if let Some(c) = e.get_child("x") {
				o.x = c.text.as_ref().unwrap().parse::<i32>().unwrap()
			}
			if let Some(c) = e.get_child("y") {
				o.y = c.text.as_ref().unwrap().parse::<i32>().unwrap()
			}
			if let Some(c) = e.get_child("primary") {
				o.primary = parse_bool(c.text.as_ref().unwrap())
			}

			o
		})
		.collect::<Vec<_>>()
	})
	.filter_map(|config| {
		let n_saved = config.len();
		if n_saved != connected_outputs.len() {
			return None;
		}

		let matched = config.into_iter()
		.filter_map(|saved| {
			connected_outputs.iter()
			.find(|connected| **connected == saved)
			.map(|connected| (connected.name.clone(), saved))
		})
		.collect::<Vec<_>>();

		if n_saved != matched.len() {
			return None;
		}

		Some(matched)
	})
	.nth(0);

	eprintln!("Matching configuration: {:?}", &configuration);

	if let Some(config) = configuration {
		let mut w = std::io::stdout();
		for (name, output) in config {
			if output.width == 0 || output.height == 0 {
				continue;
			}
			writeln!(&mut w, "output {} pos {},{} res {}x{}", name, output.x, output.y, output.width, output.height).unwrap();

			if output.primary {
				if let Some(workspace) = opts_matches.opt_str("primary-workspace") {
					writeln!(&mut w, "workspace {} output {}", workspace, name).unwrap();
				}
			}
		}
	}
}
