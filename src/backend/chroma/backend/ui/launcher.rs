use std::error::Error;

use x11rb::{protocol::xproto::ConnectionExt, rust_connection::RustConnection};

pub fn create(connection: &RustConnection, root: u32) -> Result<(), Box<dyn Error>> {
	connection.get_geometry(root)?.reply()?;
	Ok(())
}
