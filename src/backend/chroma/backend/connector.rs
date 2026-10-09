use std::error::Error;

use x11rb::{
	connection::Connection,
	protocol::{
		composite::{self, ConnectionExt as CompositeConnectionExt},
		xproto::{
			Atom, ClientMessageEvent, ConnectionExt, CreateWindowAux, EventMask, SelectionClearEvent,
			Window, WindowClass,
		},
	},
	rust_connection::RustConnection,
	CURRENT_TIME,
};

pub struct ChromaConnection {
	connection: RustConnection,
	screen_number: usize,
	overlay: Window,
	compositor_atom: Atom,
}

impl ChromaConnection {
	pub fn new(connection: RustConnection, screen_number: usize) -> Result<Self, Box<dyn Error>> {
		let screen = &connection.setup().roots[screen_number];
		let compositor_atom = connection
			.intern_atom(false, format!("_NET_WM_CM_S{screen_number}").as_bytes())?
			.reply()?
			.atom;
		let existing_owner = connection.get_selection_owner(compositor_atom)?.reply()?.owner;
		if existing_owner != 0 {
			return Err(format!("another X11 compositor already owns screen {screen_number}").into());
		}

		let overlay = connection.generate_id()?;
		connection.create_window(
			0,
			overlay,
			screen.root,
			0,
			0,
			screen.width_in_pixels,
			screen.height_in_pixels,
			0,
			WindowClass::INPUT_ONLY,
			0,
			&CreateWindowAux::new().event_mask(EventMask::PROPERTY_CHANGE),
		)?;
		connection.map_window(overlay)?;
		connection.set_selection_owner(overlay, compositor_atom, CURRENT_TIME)?;
		connection.flush()?;

		let owner = connection.get_selection_owner(compositor_atom)?.reply()?.owner;
		if owner != overlay {
			return Err("another X11 compositor already owns the screen".into());
		}

		connection.composite_redirect_subwindows(screen.root, composite::Redirect::MANUAL)?;
		connection.flush()?;

		Ok(Self { connection, screen_number, overlay, compositor_atom })
	}

	pub fn run(self) -> Result<(), Box<dyn Error>> {
		let _ = (self.screen_number, self.overlay, self.compositor_atom);
		loop {
			let event = self.connection.wait_for_event()?;
			match event {
				x11rb::protocol::Event::ClientMessage(ClientMessageEvent { .. }) => {
					// Client messages are consumed here so visual policy stays in Chroma.
				}
				x11rb::protocol::Event::SelectionClear(SelectionClearEvent { selection, .. })
					if selection == self.compositor_atom =>
				{
					return Err("Chroma lost the X11 compositor selection".into());
				}
				_ => {}
			}
		}
	}
}
