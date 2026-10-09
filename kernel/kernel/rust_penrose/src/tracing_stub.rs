//! no-op substitutes for the `tracing` macros penrose uses, so the vendored
//! window-manager logic builds under `no_std` without a logging subscriber.

pub struct Level;

impl Level {
    pub const INFO: Level = Level;
}

#[macro_export]
macro_rules! trace {
    ($($tt:tt)*) => { () };
}

#[macro_export]
macro_rules! debug {
    ($($tt:tt)*) => { () };
}

#[macro_export]
macro_rules! info {
    ($($tt:tt)*) => { () };
}

#[macro_export]
macro_rules! warn {
    ($($tt:tt)*) => { () };
}

#[macro_export]
macro_rules! error {
    ($($tt:tt)*) => { () };
}

#[macro_export]
macro_rules! span {
    ($($tt:tt)*) => { () };
}