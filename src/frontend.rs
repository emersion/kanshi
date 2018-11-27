extern crate i3ipc;

use std::error::Error;
use std::io;
use std::io::Write;

use getopts;
use self::i3ipc::I3Connection;

use backend::ConnectedOutput;
use store::SavedOutput;

#[derive(Debug)]
pub struct MatchedOutput<'a> {
	pub connected: &'a ConnectedOutput,
	pub saved: &'a SavedOutput,
}

pub trait Frontend {
	fn apply_configuration<'a>(&self, Option<&'a [MatchedOutput<'a>]>) -> Result<(), Box<Error>>;
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

	fn get_commands<'a>(&self, config: Option<&'a [MatchedOutput<'a>]>) -> Vec<String> {
		if let Some(config) = config {
			let mut cmds = Vec::with_capacity(config.len());
			for MatchedOutput{connected, saved} in config {
				if saved.enabled {
					let mut l = format!("output {} enable", &connected.name);
					l += &format!(" position {} {}", saved.x, saved.y);
					if saved.width > 0 && saved.height > 0 {
						l += &format!(" resolution {}x{}", saved.width, saved.height);
					}
					let mut v = ["90".to_string(),"180".into(),"270".into(),"flipped".into(),"flipped-90".into(),"flipped-180".into(),"flipped-270".into()];
					if v.contains(&saved.transform) {
						l += &format!(" transform {}", saved.transform.trim());
					} else {
						l += &format!(" transform normal");
					}
					if saved.scale > 0. {
						l += &format!(" scale {}", saved.scale);
					}					
					cmds.push(l);

					if saved.primary {
						if let Some(ref workspace) = self.primary_workspace {
							cmds.push(format!("workspace {} output {}", workspace, &connected.name));
						}
					}
				} else {
					cmds.push(format!("output {} disable", &connected.name));
				}
			}
			cmds
		} else {
			Vec::new()
		}
	}

	fn print_configuration<'a>(&self, config: Option<&'a [MatchedOutput<'a>]>) -> Result<(), Box<Error>> {
		let cmds = self.get_commands(config);
		let mut w = io::stdout();
		for cmd in cmds {
			let l = cmd + "\n";
			w.write(l.as_bytes())?;
		}
		Ok(())
	}
}

impl Frontend for SwayFrontend {
	fn apply_configuration<'a>(&self, config: Option<&'a [MatchedOutput<'a>]>) -> Result<(), Box<Error>> {
		let cmds = self.get_commands(config);
		let mut conn = I3Connection::connect()?;
		for cmd in cmds {
			conn.run_command(&cmd)?;
		}
		Ok(())
	}
}
