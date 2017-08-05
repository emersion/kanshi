extern crate xmltree;

use std::error::Error;
use std::env;
use std::path::PathBuf;
use std::fs::File;

#[derive(Debug, Default)]
pub struct SavedOutput {
	pub name: String,
	pub vendor: String,
	pub product: String,
	pub serial: String,

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

fn parse_bool(s: &str) -> bool {
	s == "yes"
}

pub struct GnomeStore;

impl Store for GnomeStore {
	fn list_configurations(&self) -> Result<Vec<Vec<SavedOutput>>, Box<Error>> {
		let user_config_path = env::var("XDG_CONFIG_HOME")
		.map(PathBuf::from)
		.unwrap_or(env::home_dir().unwrap().join(".config"));
		let monitors_path = user_config_path.join("monitors.xml");

		let monitors = xmltree::Element::parse(File::open(&monitors_path)?).unwrap();
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
			.collect::<Vec<_>>()
		})
		.collect::<Vec<_>>();

		Ok(configurations)
	}
}
