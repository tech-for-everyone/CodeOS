use std::error::Error;

use x11rb::{protocol::xproto::ConnectionExt, rust_connection::RustConnection};

pub fn refresh(connection: &RustConnection, root: u32, panel_height: u16) -> Result<(), Box<dyn Error>> {
	let geometry = connection.get_geometry(root)?.reply()?;
	let _panel_height = panel_height.min(geometry.height);
	Ok(())
}
