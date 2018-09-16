extern crate edid;

use std::error::Error;
use std::fmt;
use std::fs::{File, read_dir};
use std::io::prelude::*;

#[derive(Debug)]
pub struct ConnectedOutput {
	pub name: String,
	pub edid: edid::EDID,
}

impl ConnectedOutput {
	pub fn vendor(&self) -> String {
		self.edid.header.vendor[..].iter().collect::<String>()
	}

	pub fn product(&self) -> String {
		self.edid.descriptors.iter()
		.filter_map(|d| match d {
			&edid::Descriptor::ProductName(ref s) => Some(s.to_string()),
			_ => None,
		})
		.nth(0)
		.unwrap_or(format!("0x{:X}", self.edid.header.product))
	}

	pub fn serial(&self) -> String {
		self.edid.descriptors.iter()
		.filter_map(|d| match d {
			&edid::Descriptor::SerialNumber(ref s) => Some(s.to_string()),
			_ => None,
		})
		.nth(0)
		.unwrap_or(format!("0x{:X}", self.edid.header.serial))
	}
}

impl fmt::Display for ConnectedOutput {
	fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
		write!(f, "output {} vendor {} product {} serial {}", self.name, self.vendor(), self.product(), self.serial())
	}
}

pub trait Backend {
	fn list_outputs(&self) -> Result<Vec<ConnectedOutput>, Box<Error>>;
}

pub struct SysFsBackend;

const OUTPUT_PREFIX: &str = "card0-";

impl Backend for SysFsBackend {
	fn list_outputs(&self) -> Result<Vec<ConnectedOutput>, Box<Error>> {
		let outputs = read_dir("/sys/class/drm")?
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

		Ok(outputs)
	}
}
