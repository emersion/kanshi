extern crate nom;
extern crate xmltree;

use std::env;
use std::error::Error;
use std::fs::File;
use std::io::prelude::*;
use std::path::PathBuf;
use std::str;
use std::str::FromStr;

use nom::digit;

#[derive(Debug, Default)]
pub struct SavedOutput {
	pub name: String,
	pub vendor: String,
	pub product: String,
	pub serial: String,

	pub enabled: bool,
	pub width: i32,
	pub height: i32,
	pub rate: f32,
	pub x: i32,
	pub y: i32,
	//pub rotation: Rotation,
	//pub reflect_x: bool,
	//pub reflect_y: bool,
	pub primary: bool,
	//pub presentation: bool,
	//pub underscanning: bool,
}

pub trait Store {
	fn list_configurations(&self) -> Result<Vec<Vec<SavedOutput>>, Box<Error>>;
}

fn xdg_config_home() -> PathBuf {
	env::var("XDG_CONFIG_HOME")
	.map(PathBuf::from)
	.unwrap_or(env::home_dir().unwrap().join(".config"))
}

fn parse_bool(s: &str) -> bool {
	s == "yes"
}

pub struct GnomeStore;

impl Store for GnomeStore {
	fn list_configurations(&self) -> Result<Vec<Vec<SavedOutput>>, Box<Error>> {
		let monitors_path = xdg_config_home().join("monitors.xml");

		let monitors = xmltree::Element::parse(File::open(&monitors_path)?).unwrap();
		let configurations = monitors.children.iter()
		.filter(|e| e.name == "configuration")
		.map(|e| {
			// TODO: <clone> support

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

				o.enabled = o.width != 0 && o.height != 0;

				o
			})
			.collect::<Vec<_>>()
		})
		.collect::<Vec<_>>();

		Ok(configurations)
	}
}

named!(parse_space, eat_separator!(&b" \t"[..]));

named!(parse_string<&[u8], String>, map_res!(
	map_res!(
		is_not_s!(" \t\r\n"),
		str::from_utf8
	),
	FromStr::from_str
));

enum OutputArg {
	Vendor(String),
	Product(String),
	Serial(String),

	Disable,
	Resolution(i32, i32),
	Position(i32, i32),
	Scale(i32),
}

fn parse_output_with_args(name: String, args: Vec<OutputArg>) -> SavedOutput {
	let mut o = SavedOutput{name, enabled: true, ..SavedOutput::default()};

	for arg in args {
		match arg {
			OutputArg::Vendor(v) => o.vendor = v,
			OutputArg::Product(p) => o.product = p,
			OutputArg::Serial(s) => o.serial = s,
			OutputArg::Disable => o.enabled = false,
			OutputArg::Resolution(w, h) => {
				o.width = w;
				o.height = h;
			},
			OutputArg::Position(x, y) => {
				o.x = x;
				o.y = y;
			},
			OutputArg::Scale(_) => (), // TODO
		}
	}

	o
}

named!(parse_disable<&[u8], OutputArg>, do_parse!(tag!("disable") >> (OutputArg::Disable)));

named!(parse_i32<&[u8], i32>, map_res!(
	map_res!(
		digit,
		str::from_utf8
	),
	i32::from_str
));

named!(parse_resolution<&[u8], OutputArg>, do_parse!(
	tag!("resolution")
	>> parse_space
	>> w: parse_i32
	>> tag!("x")
	>> h: parse_i32
	>> (OutputArg::Resolution(w, h))
));

named!(parse_position<&[u8], OutputArg>, do_parse!(
	tag!("position")
	>> parse_space
	>> x: parse_i32
	>> tag!(",")
	>> y: parse_i32
	>> (OutputArg::Position(x, y))
));

named!(parse_scale<&[u8], OutputArg>, do_parse!(
	tag!("scale")
	>> parse_space
	>> f: parse_i32
	>> (OutputArg::Scale(f))
));

named!(parse_argument<&[u8], OutputArg>, alt!(parse_disable | parse_resolution | parse_position | parse_scale));

named!(parse_output<&[u8], SavedOutput>, do_parse!(
	tag!("output")
	>> parse_space
	>> name: parse_string
	>> args: many0!(preceded!(parse_space, parse_argument))
	>> (parse_output_with_args(name, args))
));

named!(parse_configuration<&[u8], Vec<SavedOutput>>, delimited!(tag!("{"), many0!(ws!(parse_output)), tag!("}")));

named!(parse_configuration_list<&[u8], Vec<Vec<SavedOutput>>>, many0!(ws!(parse_configuration)));

pub struct KanshiStore;

impl Store for KanshiStore {
	fn list_configurations(&self) -> Result<Vec<Vec<SavedOutput>>, Box<Error>> {
		let config_path = xdg_config_home().join("kanshi").join("config");

		let mut buf = Vec::new();
		File::open(config_path).unwrap().read_to_end(&mut buf).unwrap();

		let (_, config) = parse_configuration_list(&buf).unwrap();
		Ok(config)
	}
}
