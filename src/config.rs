use std::{collections::HashSet, env, error::Error, fs, path::PathBuf};

use crate::ui::panels::PANEL_HEIGHT;
use mlua::{Lua, Table, Value};
use penrose::{core::Config, x11rb::RustConn, Color};
use serde::Deserialize;

#[derive(Debug, Deserialize)]
#[serde(default)]
pub struct HyperdeConfig {
	pub compositor: CompositorConfig,
	pub window_manager: WindowManagerConfig,
	pub applications: ApplicationsConfig,
}

#[derive(Debug, Deserialize)]
#[serde(default)]
pub struct CompositorConfig {
	pub panel_height: u16,
}

#[derive(Debug, Deserialize)]
#[serde(default)]
pub struct WindowManagerConfig {
	pub workspaces: Vec<String>,
	pub terminal_command: String,
	pub launcher_command: String,
	pub normal_border: String,
	pub focused_border: String,
	pub border_width: u32,
	pub focus_follow_mouse: bool,
	pub floating_classes: Vec<String>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(default)]
pub struct ApplicationsConfig {
	pub startup: Vec<String>,
	pub commands: Vec<ApplicationCommand>,
}

#[derive(Debug, Deserialize, Clone)]
pub struct ApplicationCommand {
	pub name: String,
	pub key: String,
	pub command: String,
	#[serde(default)]
	pub terminal: bool,
}

impl Default for HyperdeConfig {
	fn default() -> Self {
		Self {
			compositor: CompositorConfig::default(),
			window_manager: WindowManagerConfig::default(),
			applications: ApplicationsConfig::default(),
		}
	}
}

impl Default for CompositorConfig {
	fn default() -> Self {
		Self { panel_height: PANEL_HEIGHT }
	}
}

impl Default for WindowManagerConfig {
	fn default() -> Self {
		Self {
			workspaces: (1..=9).map(|number| number.to_string()).collect(),
			terminal_command: String::from("xterm"),
			launcher_command: String::new(),
			normal_border: String::from("#3c3836ff"),
			focused_border: String::from("#cc241dff"),
			border_width: 2,
			focus_follow_mouse: true,
			floating_classes: vec![String::from("dmenu"), String::from("dunst")],
		}
	}
}

impl HyperdeConfig {
	pub fn validate(&self) -> Result<(), Box<dyn Error>> {
		if self.compositor.panel_height == 0 {
			return Err("compositor.panel_height must be greater than zero".into());
		}
		if self.window_manager.border_width > 64 {
			return Err("window_manager.border_width must be 64 or less".into());
		}
		self.validate_workspaces()?;
		self.validate_applications()?;
		Color::try_from(self.window_manager.normal_border.as_str())?;
		Color::try_from(self.window_manager.focused_border.as_str())?;
		if self
			.applications
			.commands
			.iter()
			.any(|application| application.terminal)
			&& self.window_manager.terminal_command.trim().is_empty()
		{
			return Err("terminal applications require window_manager.terminal_command".into());
		}
		Ok(())
	}

	fn validate_workspaces(&self) -> Result<(), Box<dyn Error>> {
		if self.window_manager.workspaces.is_empty() {
			return Err("window_manager.workspaces cannot be empty".into());
		}
		if self.window_manager.workspaces.iter().any(|workspace| workspace.trim().is_empty()) {
			return Err("window_manager.workspaces cannot contain empty names".into());
		}
		let unique_workspaces: HashSet<&str> = self
			.window_manager
			.workspaces
			.iter()
			.map(String::as_str)
			.collect();
		if unique_workspaces.len() != self.window_manager.workspaces.len() {
			return Err("window_manager.workspaces must not contain duplicates".into());
		}
		Ok(())
	}

	pub fn validate_applications(&self) -> Result<(), Box<dyn Error>> {
		let mut names = HashSet::new();
		let mut keys = HashSet::new();
		let mut reserved_keys: HashSet<String> = [
			"M-j", "M-k", "M-S-j", "M-S-k", "M-S-q", "M-q", "M-space", "M-S-space",
			"M-S-Up", "M-S-Down", "M-S-Right", "M-S-Left", "M-Return", "M-d",
		]
		.into_iter()
		.map(String::from)
		.collect();
		for index in 1..=self.window_manager.workspaces.len() {
			reserved_keys.insert(format!("M-{index}"));
			reserved_keys.insert(format!("M-S-{index}"));
		}
		for application in &self.applications.commands {
			if application.name.trim().is_empty() {
				return Err("applications.commands entries need a name".into());
			}
			if application.key.trim().is_empty() {
				return Err(format!("application '{}' needs a key", application.name).into());
			}
			if application.command.trim().is_empty() {
				return Err(format!("application '{}' needs a command", application.name).into());
			}
			if reserved_keys.contains(application.key.trim()) {
				return Err(format!("application '{}' uses a reserved key: {}", application.name, application.key).into());
			}
			if !names.insert(application.name.as_str()) {
				return Err(format!("duplicate application name: {}", application.name).into());
			}
			if !keys.insert(application.key.as_str()) {
				return Err(format!("duplicate application key: {}", application.key).into());
			}
		}
		Ok(())
	}

	pub fn penrose_config(&self) -> Result<Config<RustConn>, Box<dyn Error>> {
		let mut config = Config::default();
		config.tags = self.window_manager.workspaces.clone();
		config.normal_border = Color::try_from(self.window_manager.normal_border.as_str())?;
		config.focused_border = Color::try_from(self.window_manager.focused_border.as_str())?;
		config.border_width = self.window_manager.border_width;
		config.focus_follow_mouse = self.window_manager.focus_follow_mouse;
		config.floating_classes = self.window_manager.floating_classes.clone();
		Ok(config)
	}
}

pub fn load() -> Result<HyperdeConfig, Box<dyn Error>> {
	let path = env::var_os("HYPERDE_CONFIG")
		.map(PathBuf::from)
		.or_else(|| {
			let toml_path = PathBuf::from("hyperde.toml");
			if toml_path.exists() {
				Some(toml_path)
			} else if PathBuf::from("conf.lua").exists() {
				Some(PathBuf::from("conf.lua"))
			} else {
				None
			}
		})
		.unwrap_or_else(|| PathBuf::from("hyperde.toml"));
	if !path.exists() {
		let configuration = HyperdeConfig::default();
		configuration.validate()?;
		return Ok(configuration);
	}

	let contents = fs::read_to_string(&path)?;
	if path.extension().and_then(|extension| extension.to_str()) == Some("lua") {
		return Ok(load_lua(&contents)?);
	}
	let configuration: HyperdeConfig = toml::from_str(&contents)?;
	configuration.validate()?;
	Ok(configuration)
}

fn load_lua(contents: &str) -> Result<HyperdeConfig, Box<dyn Error>> {
	let lua = Lua::new();
	let root: Table = lua.load(contents).eval()?;
	let mut config = HyperdeConfig::default();

	if let Some(compositor) = optional_table(&root, "compositor")? {
		config.compositor.panel_height = compositor
			.get::<_, Option<u16>>("panel_height")?
			.unwrap_or(config.compositor.panel_height);
	}
	if let Some(window_manager) = optional_table(&root, "window_manager")? {
		if let Some(workspaces) = optional_strings(&window_manager, "workspaces")? {
			config.window_manager.workspaces = workspaces;
		}
		set_string(&window_manager, "terminal_command", &mut config.window_manager.terminal_command)?;
		set_string(&window_manager, "launcher_command", &mut config.window_manager.launcher_command)?;
		set_string(&window_manager, "normal_border", &mut config.window_manager.normal_border)?;
		set_string(&window_manager, "focused_border", &mut config.window_manager.focused_border)?;
		config.window_manager.border_width = window_manager
			.get::<_, Option<u32>>("border_width")?
			.unwrap_or(config.window_manager.border_width);
		config.window_manager.focus_follow_mouse = window_manager
			.get::<_, Option<bool>>("focus_follow_mouse")?
			.unwrap_or(config.window_manager.focus_follow_mouse);
		if let Some(classes) = optional_strings(&window_manager, "floating_classes")? {
			config.window_manager.floating_classes = classes;
		}
	}
	if let Some(applications) = optional_table(&root, "applications")? {
		if let Some(startup) = optional_strings(&applications, "startup")? {
			config.applications.startup = startup;
		}
		if let Some(commands) = applications.get::<_, Option<Table>>("commands")? {
			config.applications.commands = commands
				.sequence_values::<Table>()
			.collect::<Result<Vec<_>, _>>()?
			.into_iter()
			.map(|command| {
				Ok(ApplicationCommand {
					name: command.get("name")?,
					key: command.get("key")?,
					command: command.get("command")?,
					terminal: command.get::<_, Option<bool>>("terminal")?.unwrap_or(false),
				})
			})
			.collect::<Result<Vec<_>, mlua::Error>>()?;
		}
	}

	config.validate()?;
	Ok(config)
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn defaults_are_valid() {
		assert!(HyperdeConfig::default().validate().is_ok());
	}

	#[test]
	fn rejects_duplicate_application_keys() {
		let mut config = HyperdeConfig::default();
		config.applications.commands = vec![
			ApplicationCommand { name: "one".into(), key: "M-a".into(), command: "one".into(), terminal: false },
			ApplicationCommand { name: "two".into(), key: "M-a".into(), command: "two".into(), terminal: false },
		];
		assert!(config.validate().is_err());
	}

	#[test]
	fn rejects_workspace_binding_collision() {
		let mut config = HyperdeConfig::default();
		config.applications.commands = vec![ApplicationCommand {
			name: "Collision".into(),
			key: "M-1".into(),
			command: "xterm".into(),
			terminal: false,
		}];
		assert!(config.validate().is_err());
	}

	#[test]
	fn parses_lua_application_commands() {
		let config = load_lua(r#"
			return {
				applications = { commands = {
					{ name = "Monitor", key = "M-t", command = "btop", terminal = true },
				} }
			}
		"#).expect("Lua config should parse");
		assert_eq!(config.applications.commands[0].command, "btop");
		assert!(config.applications.commands[0].terminal);
	}
}

fn optional_table<'lua>(root: &Table<'lua>, name: &str) -> Result<Option<Table<'lua>>, mlua::Error> {
	match root.get(name)? {
		Value::Table(table) => Ok(Some(table)),
		Value::Nil => Ok(None),
		_ => Err(mlua::Error::FromLuaConversionError {
			from: "value",
			to: "table",
			message: Some(format!("{name} must be a table")),
		}),
	}
}

fn optional_strings(table: &Table<'_>, name: &str) -> Result<Option<Vec<String>>, mlua::Error> {
	let values = match table.get::<_, Option<Table>>(name)? {
		Some(values) => values,
		None => return Ok(None),
	};
	Ok(Some(values.sequence_values::<String>().collect::<Result<Vec<_>, _>>()?))
}

fn set_string(table: &Table<'_>, name: &str, destination: &mut String) -> Result<(), mlua::Error> {
	if let Some(value) = table.get::<_, Option<String>>(name)? {
		*destination = value;
	}
	Ok(())
}