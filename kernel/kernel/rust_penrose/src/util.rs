//! Utility functions for use in other parts of penrose
use crate::{Result, prelude::*};

/// Run an external command.
///
/// In the CodeOS kernel there is no external process model available to the
/// window manager, so this is a no-op that always succeeds.
pub fn spawn<S: Into<String>>(_cmd: S) -> Result<()> {
    Ok(())
}

/// Run an external command with the specified command line arguments.
///
/// In the CodeOS kernel there is no external process model available to the
/// window manager, so this is a no-op that always succeeds.
pub fn spawn_with_args<S: Into<String>>(_cmd: S, _args: &[&str]) -> Result<()> {
    Ok(())
}