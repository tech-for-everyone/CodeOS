//! Setting up and responding to user defined key/mouse bindings
use crate::{
    Error, Result,
    core::{
        State,
        conn::{Conn, WinId},
    },
    prelude::*,
    pure::geometry::Point,
};
use ::core::{convert::TryFrom, fmt};

/// Some action to be run by a user key binding
pub trait KeyEventHandler<C>: Send
where
    C: Conn,
{
    /// Call this handler with the current window manager state
    fn call(&mut self, state: &mut State<C>, conn: &mut C) -> Result<()>;
}

impl<C: Conn> fmt::Debug for Box<dyn KeyEventHandler<C>> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("KeyEventHandler").finish()
    }
}

impl<F, C> KeyEventHandler<C> for F
where
    F: FnMut(&mut State<C>, &mut C) -> Result<()> + Send,
    C: Conn,
{
    fn call(&mut self, state: &mut State<C>, conn: &mut C) -> Result<()> {
        (self)(state, conn)
    }
}

/// User defined key bindings
pub type KeyBindings<C> = HashMap<<C as Conn>::KeyBindingKey, Box<dyn KeyEventHandler<C>>>;

/// An action to be run in response to a mouse event
pub trait MouseEventHandler<C>: Send
where
    C: Conn,
{
    /// Called when the [MouseState] associated with this handler is seen with a button press or
    /// release.
    fn on_mouse_event(&mut self, evt: &MouseEvent, state: &mut State<C>, x: &mut C) -> Result<()>;

    /// Called when the [ModifierKey]s associated with this handler are seen when the mouse is
    /// moving.
    fn on_motion(
        &mut self,
        evt: &MotionNotifyEvent,
        state: &mut State<C>,
        conn: &mut C,
    ) -> Result<()>;
}

impl<C: Conn> fmt::Debug for Box<dyn MouseEventHandler<C>> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("MouseEventHandler").finish()
    }
}

impl<F, C> MouseEventHandler<C> for F
where
    F: FnMut(&mut State<C>, &C) -> Result<()> + Send,
    C: Conn,
{
    fn on_mouse_event(
        &mut self,
        evt: &MouseEvent,
        state: &mut State<C>,
        conn: &mut C,
    ) -> Result<()> {
        if evt.kind == MouseEventKind::Press {
            (self)(state, conn)
        } else {
            Ok(())
        }
    }

    fn on_motion(&mut self, _: &MotionNotifyEvent, _: &mut State<C>, _: &mut C) -> Result<()> {
        Ok(())
    }
}

/// Convert a [KeyEventHandler] to a [MouseEventHandler] that runs on button `Press` events.
///
/// This allows for running pre-existing simple key handlers that do not care about press/release
/// or motion behaviour as a simplified mouse event handler.
///
/// ## Example
/// ```rust
/// use penrose::builtin::actions::floating::sink_all;
/// use penrosecrate::core::bindings::{click_handler, MouseEventHandler};
/// use penrose::x11rb::RustConn;
///
/// let handler: Box<dyn MouseEventHandler<RustConn>> =  click_handler(sink_all());
/// ```
pub fn click_handler<C: Conn + 'static>(
    kh: Box<dyn KeyEventHandler<C>>,
) -> Box<dyn MouseEventHandler<C>> {
    Box::new(MouseWrapper { inner: kh })
}

struct MouseWrapper<C: Conn> {
    inner: Box<dyn KeyEventHandler<C>>,
}

impl<C: Conn> MouseEventHandler<C> for MouseWrapper<C> {
    fn on_mouse_event(
        &mut self,
        evt: &MouseEvent,
        state: &mut State<C>,
        conn: &mut C,
    ) -> Result<()> {
        if evt.kind == MouseEventKind::Press {
            self.inner.call(state, conn)
        } else {
            Ok(())
        }
    }

    fn on_motion(&mut self, _: &MotionNotifyEvent, _: &mut State<C>, _: &mut C) -> Result<()> {
        Ok(())
    }
}

/// User defined mouse bindings
pub type MouseBindings<C> = HashMap<MouseState, Box<dyn MouseEventHandler<C>>>;

/// Abstraction layer for working with key presses
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum KeyPress {
    /// A raw character key
    Utf8(String),
    /// Return / enter key
    Return,
    /// Escape
    Escape,
    /// Tab
    Tab,
    /// Backspace
    Backspace,
    /// Delete
    Delete,
    /// PageUp
    PageUp,
    /// PageDown
    PageDown,
    /// Up
    Up,
    /// Down
    Down,
    /// Left
    Left,
    /// Right
    Right,
}

/// A u16 X key-code bitmask
pub type KeyCodeMask = u16;

/// A u8 X key-code enum value
pub type KeyCodeValue = u8;

/// A key press and held modifiers
#[derive(Debug, PartialEq, Eq, Hash, Clone, Copy, PartialOrd, Ord)]
pub struct KeyCode {
    /// The held modifier mask
    pub mask: KeyCodeMask,
    /// The key code that was held
    pub code: KeyCodeValue,
}

impl KeyCode {
    /// Create a new [KeyCode] from this one that removes the given mask
    pub fn ignoring_modifier(&self, mask: KeyCodeMask) -> KeyCode {
        KeyCode {
            mask: self.mask & !mask,
            code: self.code,
        }
    }
}

/// Known mouse buttons for binding actions
#[derive(Debug, Default, PartialEq, Eq, Hash, Clone, Copy, PartialOrd, Ord)]
pub enum MouseButton {
    /// 1
    #[default]
    Left,
    /// 2
    Middle,
    /// 3
    Right,
    /// 4
    ScrollUp,
    /// 5
    ScrollDown,
}

impl From<MouseButton> for u8 {
    fn from(b: MouseButton) -> u8 {
        match b {
            MouseButton::Left => 1,
            MouseButton::Middle => 2,
            MouseButton::Right => 3,
            MouseButton::ScrollUp => 4,
            MouseButton::ScrollDown => 5,
        }
    }
}

impl TryFrom<u8> for MouseButton {
    type Error = Error;

    fn try_from(n: u8) -> Result<Self> {
        match n {
            1 => Ok(Self::Left),
            2 => Ok(Self::Middle),
            3 => Ok(Self::Right),
            4 => Ok(Self::ScrollUp),
            5 => Ok(Self::ScrollDown),
            _ => Err(Error::UnknownMouseButton { button: n }),
        }
    }
}

/// Known modifier keys for bindings
#[derive(Debug, PartialEq, Eq, Hash, Clone, Copy, PartialOrd, Ord)]
pub enum ModifierKey {
    /// Control
    Ctrl,
    /// Alt
    Alt,
    /// Shift
    Shift,
    /// Meta / super / windows
    Meta,
}

impl ModifierKey {
    /// All modifier keys in a defined iteration order.
    pub fn iter() -> [ModifierKey; 4] {
        [
            ModifierKey::Ctrl,
            ModifierKey::Alt,
            ModifierKey::Shift,
            ModifierKey::Meta,
        ]
    }

    /// All modifier keys as a slice (convenient for iterator use).
    pub fn all() -> &'static [ModifierKey; 4] {
        &[
            ModifierKey::Ctrl,
            ModifierKey::Alt,
            ModifierKey::Shift,
            ModifierKey::Meta,
        ]
    }

    fn was_held(&self, mask: u16) -> bool {
        mask & u16::from(*self) > 0
    }
}

impl From<ModifierKey> for u16 {
    fn from(m: ModifierKey) -> u16 {
        (match m {
            ModifierKey::Shift => 1 << 0,
            ModifierKey::Ctrl => 1 << 2,
            ModifierKey::Alt => 1 << 3,
            ModifierKey::Meta => 1 << 6,
        }) as u16
    }
}

impl TryFrom<&str> for ModifierKey {
    type Error = Error;

    fn try_from(s: &str) -> ::core::result::Result<Self, Self::Error> {
        match s {
            "C" => Ok(Self::Ctrl),
            "A" => Ok(Self::Alt),
            "S" => Ok(Self::Shift),
            "M" => Ok(Self::Meta),
            _ => Err(Error::UnknownModifier { name: s.to_owned() }),
        }
    }
}

/// A mouse state specification indicating the button and modifiers held
#[derive(Debug, PartialEq, Eq, Hash, Clone, PartialOrd, Ord)]
pub struct MouseState {
    /// The [MouseButton] being held
    pub button: MouseButton,
    /// All [ModifierKey]s being held
    pub modifiers: Vec<ModifierKey>,
}

impl MouseState {
    /// Construct a new MouseState
    pub fn new(button: MouseButton, mut modifiers: Vec<ModifierKey>) -> Self {
        modifiers.sort();
        Self { button, modifiers }
    }

    /// Parse raw mouse state values into a [MouseState]
    pub fn from_detail_and_state(detail: u8, state: u16) -> Result<Self> {
        Ok(Self {
            button: MouseButton::try_from(detail)?,
            modifiers: ModifierKey::all().iter().filter(|m| m.was_held(state)).copied().collect(),
        })
    }

    /// The xcb bitmask for this [MouseState]
    pub fn mask(&self) -> u16 {
        self.modifiers
            .iter()
            .fold(0, |acc, &val| acc | u16::from(val))
    }

    /// The xcb button ID for this [MouseState]
    pub fn button(&self) -> u8 {
        self.button.into()
    }
}

/// The types of mouse events represented by a MouseEvent
#[derive(Debug, Clone, Copy, Hash, PartialEq, Eq)]
pub enum MouseEventKind {
    /// A button was pressed
    Press,
    /// A button was released
    Release,
}

/// Data from a button press or motion-notify event
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct MouseEventData {
    /// The ID of the window that was contained the click
    pub id: WinId,
    /// Absolute coordinate of the event
    pub rpt: Point,
    /// Coordinate of the event relative to top-left of the window itself
    pub wpt: Point,
}

/// A mouse movement or button event
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct MouseEvent {
    /// The details of which window the event applies to and where the event occurred
    pub data: MouseEventData,
    /// The modifier and button code that was received
    pub state: MouseState,
    /// Was this press or release
    pub kind: MouseEventKind,
}

impl MouseEvent {
    /// Construct a new [MouseEvent] from raw data
    pub fn new(
        id: WinId,
        rx: i16,
        ry: i16,
        ex: i16,
        ey: i16,
        state: MouseState,
        kind: MouseEventKind,
    ) -> Self {
        MouseEvent {
            data: MouseEventData {
                id,
                rpt: Point::new(rx as i32, ry as i32),
                wpt: Point::new(ex as i32, ey as i32),
            },
            state,
            kind,
        }
    }
}

/// Mouse motion with a held button and optional modifiers
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct MotionNotifyEvent {
    /// The details of which window the event applies to and where the event occurred
    pub data: MouseEventData,
    /// All [ModifierKey]s being held
    pub modifiers: Vec<ModifierKey>,
}

impl MotionNotifyEvent {
    /// Construct a new [MotionNotifyEvent] from raw data
    pub fn new(id: WinId, rx: i16, ry: i16, ex: i16, ey: i16, modifiers: Vec<ModifierKey>) -> Self {
        MotionNotifyEvent {
            data: MouseEventData {
                id,
                rpt: Point::new(rx as i32, ry as i32),
                wpt: Point::new(ex as i32, ey as i32),
            },
            modifiers,
        }
    }
}
