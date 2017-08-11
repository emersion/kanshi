use std::error::Error;
use std::sync::mpsc::Sender;

pub trait Notifier {
	fn notify(&self, tx: Sender<()>) -> Result<(), Box<Error>>;
}
