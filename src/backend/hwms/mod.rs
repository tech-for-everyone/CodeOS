use std::{collections::HashMap, error::Error};

use penrose::{
    builtin::{
        actions::{
            exit,
            floating::{MouseDragHandler, MouseResizeHandler},
            modify_with, send_layout_message,
        },
        layout::messages::{ExpandMain, IncMain, ShrinkMain},
    },
    core::{
        bindings::{
            parse_keybindings_with_xmodmap, KeyBindings, KeyEventHandler, MouseBindings,
            MouseState,
        },
        WindowManager,
    },
    x11rb::RustConn,
};
use penrose::core::bindings::{ModifierKey, MouseButton};
use penrose::util;

use crate::config::HyperdeConfig;

pub fn run(configuration: &HyperdeConfig) -> Result<(), Box<dyn Error>> {
    configuration.validate()?;
    for command in &configuration.applications.startup {
        if !command.trim().is_empty() {
            util::spawn(command)?;
        }
    }
    let connection = RustConn::new()?;
    let key_bindings = key_bindings(configuration)?;
    let mouse_bindings = mouse_bindings();
    let window_manager = WindowManager::new(configuration.penrose_config()?, key_bindings, mouse_bindings, connection)?;
    window_manager.run()?;
    Ok(())
}

fn key_bindings(configuration: &HyperdeConfig) -> Result<KeyBindings<RustConn>, Box<dyn Error>> {
    let mut bindings: HashMap<String, Box<dyn KeyEventHandler<RustConn>>> = HashMap::new();
    bindings.insert(String::from("M-j"), modify_with(|clients| clients.focus_down()));
    bindings.insert(String::from("M-k"), modify_with(|clients| clients.focus_up()));
    bindings.insert(String::from("M-S-j"), modify_with(|clients| clients.swap_down()));
    bindings.insert(String::from("M-S-k"), modify_with(|clients| clients.swap_up()));
    bindings.insert(String::from("M-S-q"), modify_with(|clients| clients.kill_focused()));
    bindings.insert(String::from("M-q"), exit());
    bindings.insert(String::from("M-space"), modify_with(|clients| clients.next_layout()));
    bindings.insert(String::from("M-S-space"), modify_with(|clients| clients.previous_layout()));
    bindings.insert(String::from("M-S-Up"), send_layout_message(|| IncMain(1)));
    bindings.insert(String::from("M-S-Down"), send_layout_message(|| IncMain(-1)));
    bindings.insert(String::from("M-S-Right"), send_layout_message(|| ExpandMain));
    bindings.insert(String::from("M-S-Left"), send_layout_message(|| ShrinkMain));

    if !configuration.window_manager.terminal_command.trim().is_empty() {
        bindings.insert(
            String::from("M-Return"),
            spawn_command(configuration.window_manager.terminal_command.clone()),
        );
    }
    if !configuration.window_manager.launcher_command.trim().is_empty() {
        bindings.insert(
            String::from("M-d"),
            spawn_command(configuration.window_manager.launcher_command.clone()),
        );
    }
    for application in &configuration.applications.commands {
        if !application.key.trim().is_empty() && !application.command.trim().is_empty() {
            let command = application_command(configuration, application);
            bindings.insert(application.key.clone(), spawn_command(command));
        }
    }

    for (index, tag) in configuration.window_manager.workspaces.iter().enumerate() {
        let key = format!("M-{}", index + 1);
        let move_key = format!("M-S-{}", index + 1);
        let focus_tag = tag.clone();
        let move_tag = tag.clone();
        bindings.insert(key, modify_with(move |clients| clients.focus_tag(focus_tag.clone())));
        bindings.insert(move_key, modify_with(move |clients| clients.move_focused_to_tag(move_tag.clone())));
    }

    Ok(parse_keybindings_with_xmodmap(bindings)?)
}

fn application_command(configuration: &HyperdeConfig, application: &crate::config::ApplicationCommand) -> String {
    if application.terminal {
        format!("{} -e {}", configuration.window_manager.terminal_command, application.command)
    } else {
        application.command.clone()
    }
}

fn spawn_command(command: String) -> Box<dyn KeyEventHandler<RustConn>> {
    penrose::builtin::actions::key_handler(move |_, _| util::spawn(command.clone()))
}

fn mouse_bindings() -> MouseBindings<RustConn> {
    let mut bindings = HashMap::new();
    let modifiers = vec![ModifierKey::Shift, ModifierKey::Meta];
    bindings.insert(
        MouseState { button: MouseButton::Left, modifiers: modifiers.clone() },
        MouseDragHandler::boxed_default(),
    );
    bindings.insert(
        MouseState { button: MouseButton::Right, modifiers },
        MouseResizeHandler::boxed_default(),
    );
    bindings
}