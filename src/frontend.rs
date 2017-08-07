use std::error::Error;
use std::io;
use std::io::Write;

use getopts;

use backend::ConnectedOutput;
use store::SavedOutput;

#[derive(Debug)]
pub struct MatchedOutput<'a> {
	pub connected: &'a ConnectedOutput,
	pub saved: &'a SavedOutput,
}

pub trait Frontend {
	fn apply_configuration(&self, Option<Vec<MatchedOutput>>) -> Result<(), Box<Error>>;
}

pub struct SwayFrontend {
	primary_workspace: Option<String>,
}

impl SwayFrontend {
	pub fn new(opts: getopts::Matches) -> SwayFrontend {
		SwayFrontend{
			primary_workspace: opts.opt_str("primary-workspace"),
		}
	}
}

impl Frontend for SwayFrontend {
	fn apply_configuration(&self, config: Option<Vec<MatchedOutput>>) -> Result<(), Box<Error>> {
		if let Some(config) = config {
			let mut w = io::stdout();
			for MatchedOutput{connected, saved} in config {
				if saved.enabled {
					writeln!(&mut w, "output {} pos {},{} res {}x{}", connected.name, saved.x, saved.y, saved.width, saved.height).unwrap();

					if saved.primary {
						if let Some(ref workspace) = self.primary_workspace {
							writeln!(&mut w, "workspace {} output {}", workspace, connected.name).unwrap();
						}
					}
				} else {
					writeln!(&mut w, "output {} disable", connected.name).unwrap();
				}
			}
		}

		Ok(())
	}
}
