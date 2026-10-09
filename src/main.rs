mod backend;
mod config;
mod ui;

use std::{env, error::Error, process::ExitCode};

fn print_usage() {
	eprintln!("usage: hyperde <chroma|hwms>\n\nUses hyperde.toml, or conf.lua when TOML is absent. Set HYPERDE_CONFIG to choose a file.");
}

fn main() -> ExitCode {
	let mut arguments = env::args().skip(1);
	let component = arguments.next();
	if matches!(component.as_deref(), Some("help" | "--help" | "-h")) {
		print_usage();
		return ExitCode::SUCCESS;
	}

	let configuration = match config::load() {
		Ok(configuration) => configuration,
		Err(error) => {
			eprintln!("hyperde: could not load configuration: {error}");
			return ExitCode::FAILURE;
		}
	};
	let result = match component.as_deref() {
		Some("chroma") => backend::chroma::run(&configuration),
		Some("hwms") => backend::hwms::run(&configuration),
		_ => {
			print_usage();
			Err::<(), Box<dyn Error>>("a component must be selected".into())
		}
	};

	match result {
		Ok(()) => ExitCode::SUCCESS,
		Err(error) => {
			eprintln!("hyperde: {error}");
			ExitCode::FAILURE
		}
	}
}
