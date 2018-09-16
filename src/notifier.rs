extern crate libudev;

use std::error::Error;
use std::sync::mpsc::Sender;
use std::thread;
use std::time::Duration;

pub trait Notifier {
	// TODO: use blocking I/O instead
	fn notify(&self, tx: Sender<()>) -> Result<(), Box<Error>>;
}

pub struct UdevNotifier {}

impl Notifier for UdevNotifier {
	fn notify(&self, tx: Sender<()>) -> Result<(), Box<Error>> {
		thread::spawn(move || {
			let ctx = libudev::Context::new().unwrap();
			let mut mon = libudev::Monitor::new(&ctx).unwrap();
			mon.match_subsystem("drm").unwrap();
			let mut socket = mon.listen().unwrap();

			loop {
				let _ = match socket.receive_event() {
					Some(evt) => evt,
					None => {
						// TODO: poll socket instead
						thread::sleep(Duration::from_millis(1000));
						continue;
					}
				};

				tx.send(()).unwrap();
			}
		});

		Ok(())
	}
}
