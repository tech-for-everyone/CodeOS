//! Side effect free management of internal window manager state

mod diff;
pub mod geometry;
mod screen;
mod stack;
mod stack_set;
mod workspace;

#[doc(inline)]
pub use diff::{Diff, Snapshot};
#[doc(inline)]
pub use screen::{Screen, ScreenClients};
#[doc(inline)]
pub use stack::{Position, Stack};
#[doc(inline)]
pub use stack_set::StackSet;
#[doc(inline)]
pub use workspace::Workspace;

