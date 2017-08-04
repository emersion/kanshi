extern crate xmltree;
extern crate edid;

use std::env;
use std::fs;
use std::fs::File;
use std::io::prelude::*;
use std::path::PathBuf;

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

fn parse_bool(s: &str) -> bool {
	s == "yes"
}

fn main() {
	let output_prefix = "card0-";

	let outputs = fs::read_dir("/sys/class/drm").unwrap()
	.map(|r| r.unwrap())
	.filter(|e| e.file_name().to_str().unwrap().starts_with(output_prefix))
	.filter(|e| {
		let mut status = String::new();
		File::open(e.path().join("status")).unwrap().read_to_string(&mut status).unwrap();
		status.trim() == "connected"
	})
	.map(|e| {
		let name = e.file_name().to_str().unwrap().trim_left_matches(output_prefix).to_string();

		let mut buf = Vec::new();
		File::open(e.path().join("edid")).unwrap().read_to_end(&mut buf).unwrap();
		let (_, edid) = edid::parse(&buf).unwrap();

		ConnectedOutput{name, edid}
	})
	.collect::<Vec<_>>();
	println!("{:?}", outputs);

	let user_config_path = env::var("XDG_CONFIG_HOME")
	.map(PathBuf::from)
	.unwrap_or(env::home_dir().unwrap().join(".config"));
	let monitors_path = user_config_path.join("monitors.xml");

	let monitors = Element::parse(File::open(&monitors_path).unwrap()).unwrap();
	let configurations = monitors.children.iter()
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
	});
	for configuration in configurations {
		println!("{:?}", configuration.collect::<Vec<_>>());
	}
}
