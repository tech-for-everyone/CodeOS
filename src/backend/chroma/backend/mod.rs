pub mod connector;
pub mod ui;

use std::error::Error;

use x11rb::{connection::Connection, rust_connection::RustConnection};

use crate::config::HyperdeConfig;

pub fn run(configuration: &HyperdeConfig) -> Result<(), Box<dyn Error>> {
	let (connection, screen_number) = RustConnection::connect(None)?;
	let root = connection.setup().roots[screen_number].root;
	ui::launcher::create(&connection, root)?;
	ui::panels::refresh(&connection, root, configuration.compositor.panel_height)?;
	let chroma = connector::ChromaConnection::new(connection, screen_number)?;
	chroma.run()?;
	Ok(())
}