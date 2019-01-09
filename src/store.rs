extern crate nom;
extern crate xmltree;

use std::env;
use std::error::Error;
use std::fs::File;
use std::io::prelude::*;
use std::path::PathBuf;
use std::str;
use std::str::FromStr;

#[derive(Debug)]
pub enum Transform {
	Rotation(i32),
	FlippedRotation(i32)
}
impl Default for Transform {
	fn default() -> Transform {
		Transform::Rotation(0)
	}
}
impl Transform {
	fn from_str(s: &str) -> Transform {
		match s {
			"90" => Transform::Rotation(90),
			"180" => Transform::Rotation(180),
			"270" => Transform::Rotation(270),
			"flipped" => Transform::FlippedRotation(0),
			"flipped-90" => Transform::FlippedRotation(90),
			"flipped-180" => Transform::FlippedRotation(180),
			"flipped-270" => Transform::FlippedRotation(270),
			_ => Transform::Rotation(0)
		}
	}
}

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
	pub transform: Transform,
	//pub reflect_x: bool,
	//pub reflect_y: bool,
	pub primary: bool,
	//pub presentation: bool,
	//pub underscanning: bool,
	pub scale: f32,
}

#[derive(Debug, Default)]
pub struct SavedConfiguration {
	pub outputs: Vec<SavedOutput>,
	pub exec: Vec<Vec<String>>,
}

pub trait Store {
	fn list_configurations(&self) -> Result<Vec<SavedConfiguration>, Box<Error>>;
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
	fn list_configurations(&self) -> Result<Vec<SavedConfiguration>, Box<Error>> {
		let monitors_path = xdg_config_home().join("monitors.xml");

		let monitors = xmltree::Element::parse(File::open(&monitors_path)?).unwrap();
		let configurations = monitors.children.iter()
		.filter(|e| e.name == "configuration")
		.map(|e| {
			// TODO: <clone> support

			let outputs = e.children.iter()
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
				if let Some(c) = e.get_child("rotation") {
					match c.text.as_ref().unwrap().trim() {
						"right" => o.transform = Transform::Rotation(90),
						"inverted" => o.transform = Transform::Rotation(180),
						"left" => o.transform = Transform::Rotation(270),
						_ => o.transform = Transform::Rotation(0),
					};
				}
				if let Some(c) = e.get_child("primary") {
					o.primary = parse_bool(c.text.as_ref().unwrap())
				}

				o.enabled = o.width != 0 && o.height != 0;

				o
			})
			.collect::<Vec<_>>();

			SavedConfiguration{outputs, exec: Vec::new()}
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
	Transform(Transform),
	Scale(f32),
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
			OutputArg::Transform(t) => o.transform = t,
			OutputArg::Scale(f) => o.scale = f,
		}
	}

	o
}

named!(parse_vendor<&[u8], OutputArg>, do_parse!(
	tag!("vendor")
	>> parse_space
	>> v: parse_string
	>> (OutputArg::Vendor(v))
));

named!(parse_product<&[u8], OutputArg>, do_parse!(
	tag!("product")
	>> parse_space
	>> p: parse_string
	>> (OutputArg::Product(p))
));

named!(parse_serial<&[u8], OutputArg>, do_parse!(
	tag!("serial")
	>> parse_space
	>> s: parse_string
	>> (OutputArg::Serial(s))
));

named!(parse_disable<&[u8], OutputArg>, do_parse!(tag!("disable") >> (OutputArg::Disable)));

named!(parse_i32<&[u8], i32>, map_res!(
	map_res!(
		nom::digit,
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

named!(parse_transform<&[u8], OutputArg>, do_parse!(
	tag!("transform")
	>> parse_space
	>> t: parse_string
	>> (OutputArg::Transform(Transform::from_str(&t)))
));

named!(parse_f32<&[u8], f32>, ws!(nom::float));

named!(parse_scale<&[u8], OutputArg>, do_parse!(
	tag!("scale")
	>> parse_space
	>> f: parse_f32
	>> (OutputArg::Scale(f))
));

named!(parse_output_arg<&[u8], OutputArg>, alt!(
	parse_vendor | parse_product | parse_serial |
	parse_disable | parse_resolution | parse_position | parse_transform | parse_scale
));

enum ConfigurationArg {
	Output(SavedOutput),
	Exec(Vec<String>),
}

fn parse_configuration_with_args(args: Vec<ConfigurationArg>) -> SavedConfiguration {
	let mut c = SavedConfiguration::default();

	for arg in args {
		match arg {
			ConfigurationArg::Output(o) => c.outputs.push(o),
			ConfigurationArg::Exec(e) => c.exec.push(e),
		}
	}

	c
}

named!(parse_output_name<&[u8], String>, map!(
	parse_string,
	|s| {
		if s == "*" {
			String::from("")
		} else {
			s
		}
	}
));

named!(parse_output<&[u8], ConfigurationArg>, do_parse!(
	tag!("output")
	>> parse_space
	>> name: parse_output_name
	>> args: many0!(preceded!(parse_space, parse_output_arg))
	>> (ConfigurationArg::Output(parse_output_with_args(name, args)))
));

named!(parse_exec<&[u8], ConfigurationArg>, do_parse!(
	tag!("exec")
	>> cmd: many1!(preceded!(parse_space, parse_string))
	>> (ConfigurationArg::Exec(cmd))
));

named!(parse_configuration_arg<&[u8], ConfigurationArg>, alt!(parse_output | parse_exec));

named!(parse_configuration_args<&[u8], Vec<ConfigurationArg>>, delimited!(
	tag!("{"),
	many0!(ws!(parse_configuration_arg)),
	tag!("}")
));

named!(parse_configuration<&[u8], SavedConfiguration>, map!(
	parse_configuration_args, parse_configuration_with_args
));

named!(parse_configuration_list<&[u8], Vec<SavedConfiguration>>, many0!(ws!(parse_configuration)));

pub struct KanshiStore;

impl Store for KanshiStore {
	fn list_configurations(&self) -> Result<Vec<SavedConfiguration>, Box<Error>> {
		let config_path = xdg_config_home().join("kanshi").join("config");

		let mut buf = Vec::new();
		File::open(config_path).unwrap().read_to_end(&mut buf).unwrap();

		let (_, config) = parse_configuration_list(&buf).unwrap();
		Ok(config)
	}
}
