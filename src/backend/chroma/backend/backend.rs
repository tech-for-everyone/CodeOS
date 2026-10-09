use std::error::Error;

use x11rb::{connection::Connection, rust_connection::RustConnection};

use crate::config::HyperdeConfig;
use super::{connector::ChromaConnection, ui::{launcher, panels}};

pub fn run(configuration: &HyperdeConfig) -> Result<(), Box<dyn Error>> {
	let (connection, screen_number) = RustConnection::connect(None)?;
	let root = connection.setup().roots[screen_number].root;
	launcher::create(&connection, root)?;
	panels::refresh(&connection, root, configuration.compositor.panel_height)?;
	let chroma = ChromaConnection::new(connection, screen_number)?;
	chroma.run()?;
	Ok(())
}
