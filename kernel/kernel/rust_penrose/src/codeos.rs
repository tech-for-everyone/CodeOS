//! The real CodeOS transport for penrose: a [`Conn`] bound to the kernel X11
//! server and HyperDE compositor via the C bridge in `penrose_bridge.c`.

use crate::pure::geometry::{Point, Rect};
use crate::{
    Color, Result, WinId,
    core::{
        Config, State,
        bindings::{
            KeyBindings, KeyCode, MouseBindings, MouseEvent, MouseEventKind, MouseState,
            MotionNotifyEvent,
        },
        conn::{Conn, ConnExt},
    },
    prelude::*,
};
use ::core::fmt;

// ---------------------------------------------------------------------------
// Raw repr(C) mirror of the C bridge event struct
// ---------------------------------------------------------------------------
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct PrsEvent {
    pub type_: i32,
    pub window: u32,
    pub detail: i32,
    pub state: u16,
    pub mx: i16,
    pub my: i16,
    pub cx: i16,
    pub cy: i16,
    pub cw: i16,
    pub ch: i16,
}

// ---------------------------------------------------------------------------
// CodeOSEvent
// ---------------------------------------------------------------------------

/// A window manager event for the CodeOS connection.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum CodeOSEvent {
    MapRequest { id: WinId },
    UnmapNotify { id: WinId },
    DestroyNotify { id: WinId },
    ConfigureRequest { id: WinId, rect: Rect },
    FocusRequest { id: WinId },
    KeyPress { code: KeyCode },
    KeyRelease { code: KeyCode },
    ButtonPress { button: u8, mask: u16, x: i16, y: i16 },
    ButtonRelease { button: u8, mask: u16, x: i16, y: i16 },
    Motion { x: i16, y: i16, mask: u16 },
}

impl fmt::Display for CodeOSEvent {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::MapRequest { id } => write!(f, "MapRequest({id})"),
            Self::UnmapNotify { id } => write!(f, "UnmapNotify({id})"),
            Self::DestroyNotify { id } => write!(f, "DestroyNotify({id})"),
            Self::ConfigureRequest { id, .. } => write!(f, "ConfigureRequest({id})"),
            Self::FocusRequest { id } => write!(f, "FocusRequest({id})"),
            Self::KeyPress { code } => write!(f, "KeyPress({code:?})"),
            Self::KeyRelease { code } => write!(f, "KeyRelease({code:?})"),
            Self::ButtonPress { button, .. } => write!(f, "ButtonPress({button})"),
            Self::ButtonRelease { button, .. } => write!(f, "ButtonRelease({button})"),
            Self::Motion { .. } => write!(f, "Motion"),
        }
    }
}

impl crate::core::conn::ConnEvent for CodeOSEvent {
    fn requires_pointer_warp(&self) -> bool {
        matches!(self, Self::KeyPress { .. } | Self::ButtonPress { .. })
    }
}

// ---------------------------------------------------------------------------
// Per-connection state
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub struct CodeOSConnState {
    pub root: WinId,
    pub screens: Vec<Rect>,
}

impl Default for CodeOSConnState {
    fn default() -> Self {
        Self {
            root: WinId(1),
            screens: vec![Rect::new(0, 28, 1280, 692)],
        }
    }
}

// ---------------------------------------------------------------------------
// CodeOSConn
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub struct CodeOSConn {
    state: CodeOSConnState,
}

impl CodeOSConn {
    pub fn try_new() -> Result<Self> {
        let mut state = CodeOSConnState::default();
        unsafe {
            prs_init();
            let root = prs_root();
            if root != 0 {
                state.root = WinId(root);
            }
            let n = prs_screen_count();
            if n > 0 {
                let mut x = 0i32;
                let mut y = 0i32;
                let mut w = 0i32;
                let mut h = 0i32;
                if prs_screen_geom(0, &mut x, &mut y, &mut w, &mut h) == 0 {
                    state.screens = vec![Rect::new(x, y, w as u32, h as u32)];
                }
            }
        }
        Ok(Self { state })
    }
}

impl Default for CodeOSConn {
    fn default() -> Self {
        Self::try_new().expect("CodeOSConn::default")
    }
}

// ---------------------------------------------------------------------------
// event conversion
// ---------------------------------------------------------------------------

fn event_to_codeos(e: PrsEvent) -> Option<CodeOSEvent> {
    match e.type_ {
        1 => Some(CodeOSEvent::MapRequest {
            id: WinId(e.window),
        }),
        2 => Some(CodeOSEvent::UnmapNotify {
            id: WinId(e.window),
        }),
        3 => Some(CodeOSEvent::DestroyNotify {
            id: WinId(e.window),
        }),
        4 => Some(CodeOSEvent::ConfigureRequest {
            id: WinId(e.window),
            rect: Rect::new(e.cx as i32, e.cy as i32, e.cw as u32, e.ch as u32),
        }),
        5 => Some(CodeOSEvent::KeyPress {
            code: KeyCode {
                mask: e.state,
                code: e.detail as u8,
            },
        }),
        6 => Some(CodeOSEvent::KeyRelease {
            code: KeyCode {
                mask: e.state,
                code: e.detail as u8,
            },
        }),
        7 => Some(CodeOSEvent::ButtonPress {
            button: e.detail as u8,
            mask: e.state,
            x: e.mx,
            y: e.my,
        }),
        8 => Some(CodeOSEvent::ButtonRelease {
            button: e.detail as u8,
            mask: e.state,
            x: e.mx,
            y: e.my,
        }),
        9 => Some(CodeOSEvent::Motion {
            x: e.mx,
            y: e.my,
            mask: e.state,
        }),
        _ => None,
    }
}

// ---------------------------------------------------------------------------
// mouse helper
// ---------------------------------------------------------------------------

impl CodeOSConn {
    fn run_mouse(
        &mut self,
        button: u8,
        mask: u16,
        x: i16,
        y: i16,
        kind: MouseEventKind,
        bindings: &mut MouseBindings<Self>,
        state: &mut State<Self>,
    ) -> Result<()> {
        let ms = match MouseState::from_detail_and_state(button, mask) {
            Ok(m) => m,
            Err(_) => return Ok(()),
        };
        let id = state
            .client_set
            .current_client()
            .copied()
            .unwrap_or(state.root);
        let evt = MouseEvent::new(id, x, y, x, y, ms.clone(), kind);
        if let Some(action) = bindings.get_mut(&ms) {
            action.on_mouse_event(&evt, state, self)?;
            match kind {
                MouseEventKind::Press => state.held_mouse_state = Some(ms),
                MouseEventKind::Release => state.held_mouse_state = None,
            }
        }
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// Conn impl
// ---------------------------------------------------------------------------

impl Conn for CodeOSConn {
    type Event = CodeOSEvent;
    type State = CodeOSConnState;
    type KeyBindingKey = KeyCode;

    fn initial_state(&mut self) -> Self::State {
        self.state.clone()
    }

    fn root(&mut self) -> WinId {
        self.state.root
    }

    fn next_event(&mut self) -> Result<Self::Event> {
        loop {
            let e = unsafe { prs_event_pop() };
            if e.type_ == 0 {
                return Err(crate::Error::Custom("no events available".into()));
            }
            if let Some(evt) = event_to_codeos(e) {
                return Ok(evt);
            }
        }
    }

    fn handle_event(
        &mut self,
        evt: Self::Event,
        key_bindings: &mut KeyBindings<Self>,
        mouse_bindings: &mut MouseBindings<Self>,
        state: &mut State<Self>,
    ) -> Result<()> {
        match evt {
            CodeOSEvent::MapRequest { id } => {
                if !state.client_set.contains(&id) && self.client_should_be_managed(id) {
                    self.manage(id, state)?;
                }
            }
            CodeOSEvent::UnmapNotify { id } | CodeOSEvent::DestroyNotify { id } => {
                if state.client_set.contains(&id) {
                    self.unmanage(id, state)?;
                }
            }
            CodeOSEvent::FocusRequest { id } => {
                if state.client_set.contains(&id) {
                    self.set_active_client(id, state)?;
                }
            }
            CodeOSEvent::ConfigureRequest { id, rect } => {
                if state.client_set.contains(&id)
                    && !state.client_set.floating.contains_key(&id)
                {
                    return Ok(()); // managed tiled clients can't self-configure
                }
                self.position_client(id, rect)?;
            }
            CodeOSEvent::KeyPress { code } => {
                if let Some(action) = key_bindings.get_mut(&code) {
                    action.call(state, self)?;
                }
            }
            CodeOSEvent::KeyRelease { .. } => {}
            CodeOSEvent::ButtonPress {
                button,
                mask,
                x,
                y,
            } => {
                self.run_mouse(button, mask, x, y, MouseEventKind::Press, mouse_bindings, state)?;
            }
            CodeOSEvent::ButtonRelease {
                button,
                mask,
                x,
                y,
            } => {
                self.run_mouse(
                    button,
                    mask,
                    x,
                    y,
                    MouseEventKind::Release,
                    mouse_bindings,
                    state,
                )?;
            }
            CodeOSEvent::Motion { x, y, mask: _ } => {
                let held = match state.held_mouse_state.clone() {
                    Some(s) => s,
                    None => return Ok(()),
                };
                let id = state
                    .client_set
                    .current_client()
                    .copied()
                    .unwrap_or(state.root);
                let mods = held.modifiers.clone();
                if let Some(action) = mouse_bindings.get_mut(&held) {
                    let e = MotionNotifyEvent::new(id, x, y, x, y, mods);
                    action.on_motion(&e, state, self)?;
                }
            }
        }
        Ok(())
    }

    fn flush(&mut self) {}

    fn grab(&mut self, _key_codes: &[KeyCode], _mouse_states: &[MouseState]) -> Result<()> {
        Ok(())
    }

    fn existing_clients(&mut self) -> Result<Vec<WinId>> {
        let mut buf = [0u32; 32];
        let n = unsafe { prs_existing_clients(buf.as_mut_ptr(), buf.len() as i32) };
        Ok(buf[..n as usize].iter().map(|&id| WinId(id)).collect())
    }

    fn manage_existing_clients(&mut self, state: &mut State<Self>) -> Result<()> {
        let ids = self.existing_clients()?;
        for id in ids {
            if self.client_should_be_managed(id) {
                self.manage(id, state)?;
            }
        }
        Ok(())
    }

    fn screen_details(&mut self) -> Result<Vec<Rect>> {
        Ok(self.state.screens.clone())
    }

    fn cursor_position(&mut self) -> Result<Point> {
        let r = self
            .state
            .screens
            .first()
            .copied()
            .unwrap_or_default();
        Ok(Point::new(
            r.x + r.w as i32 / 2,
            r.y + r.h as i32 / 2,
        ))
    }

    fn warp_pointer(&mut self, _id: WinId, _x: i16, _y: i16) -> Result<()> {
        Ok(())
    }

    fn position_client(&mut self, id: WinId, r: Rect) -> Result<()> {
        unsafe {
            prs_position_client(*id, r.x, r.y, r.w as i32, r.h as i32);
        }
        Ok(())
    }

    fn show_client(&mut self, id: WinId, _state: &mut State<Self>) -> Result<()> {
        unsafe { prs_show_client(*id) };
        Ok(())
    }

    fn hide_client(&mut self, id: WinId, _state: &mut State<Self>) -> Result<()> {
        unsafe { prs_hide_client(*id) };
        Ok(())
    }

    fn withdraw_client(&mut self, id: WinId) -> Result<()> {
        unsafe { prs_withdraw_client(*id) };
        Ok(())
    }

    fn kill_client(&mut self, id: WinId) -> Result<()> {
        unsafe { prs_kill_client(*id) };
        Ok(())
    }

    fn focus_client(&mut self, id: WinId) -> Result<()> {
        unsafe { prs_focus_client(*id) };
        Ok(())
    }

    fn client_geometry(&mut self, id: WinId) -> Result<Rect> {
        let mut x = 0i32;
        let mut y = 0i32;
        let mut w = 0i32;
        let mut h = 0i32;
        if unsafe { prs_client_geom(*id, &mut x, &mut y, &mut w, &mut h) } != 0 {
            return Err(crate::Error::Custom("client_geometry failed".into()));
        }
        Ok(Rect::new(x, y, w as u32, h as u32))
    }

    fn client_title(&mut self, id: WinId) -> Result<String> {
        let mut buf = [0u8; 64];
        let n = unsafe { prs_client_title(*id, buf.as_mut_ptr(), buf.len() as i32) };
        if n < 0 {
            return Ok(String::from("CodeOS client"));
        }
        let slice = &buf[..n as usize];
        Ok(String::from_utf8_lossy(slice).into_owned())
    }

    fn client_pid(&mut self, id: WinId) -> Option<u32> {
        let pid = unsafe { prs_client_pid(*id) };
        if pid != 0 {
            Some(pid)
        } else {
            None
        }
    }

    fn client_should_float(&mut self, _id: WinId, _floating_classes: &[String]) -> bool {
        false
    }

    fn client_should_be_managed(&mut self, _id: WinId) -> bool {
        true
    }

    fn client_is_fullscreen(&mut self, _id: WinId) -> bool {
        false
    }

    fn client_transient_parent(&mut self, _id: WinId) -> Option<WinId> {
        None
    }

    fn set_client_border_color(&mut self, _id: WinId, _color: impl Into<Color>) -> Result<()> {
        Ok(())
    }

    fn set_initial_properties(&mut self, _id: WinId, _config: &Config<Self>) -> Result<()> {
        Ok(())
    }

    fn restack<'a, I>(&mut self, _ids: I) -> Result<()>
    where
        WinId: 'a,
        I: Iterator<Item = &'a WinId>,
    {
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// External C bridge symbols
// ---------------------------------------------------------------------------

extern "C" {
    fn prs_init();
    fn prs_event_pop() -> PrsEvent;
    fn prs_event_count() -> i32;
    fn prs_root() -> u32;
    fn prs_screen_count() -> i32;
    fn prs_screen_geom(i: i32, x: *mut i32, y: *mut i32, w: *mut i32, h: *mut i32) -> i32;
    fn prs_existing_clients(buf: *mut u32, max: i32) -> i32;
    fn prs_client_geom(xid: u32, x: *mut i32, y: *mut i32, w: *mut i32, h: *mut i32) -> i32;
    fn prs_client_title(xid: u32, buf: *mut u8, max: i32) -> i32;
    fn prs_client_pid(xid: u32) -> u32;
    fn prs_position_client(xid: u32, x: i32, y: i32, w: i32, h: i32);
    fn prs_show_client(xid: u32);
    fn prs_hide_client(xid: u32);
    fn prs_withdraw_client(xid: u32);
    fn prs_focus_client(xid: u32);
    fn prs_kill_client(xid: u32);
}

// ---------------------------------------------------------------------------
// FFI exports: called from the C desktop loop
// ---------------------------------------------------------------------------

/// Initialize the penrose WM. Called once from `qt_panels_manager.cpp` at
/// desktop startup after the compositor and X11 server are ready.
///
/// # Safety
/// Must only be called once.
#[no_mangle]
pub unsafe extern "C" fn penrose_init() {
    use crate::core::WindowManager;

    static mut CALL_ID: i32 = 0;
    let id = unsafe {
        CALL_ID += 1;
        CALL_ID
    };
    unsafe {
        crate::kprintf(b"penrose: start #%d\n\0".as_ptr(), id as usize, 0, 0, 0, 0);
    }

    let conn = match CodeOSConn::try_new() {
        Ok(c) => c,
        Err(e) => {
            error!("penrose_init: failed to create CodeOSConn");
            return;
        }
    };
    klog("penrose: conn ok\n");

    let mut wm = match WindowManager::new(
        Config::default(),
        KeyBindings::new(),
        MouseBindings::new(),
        conn,
    ) {
        Ok(w) => w,
        Err(e) => {
            error!("penrose_init: failed to create WindowManager");
            return;
        }
    };
    klog("penrose: init ok\n");

    // Spawn demo X11 windows via the bridge. Their MapRequests are queued and
    // drained below so the WM manages + tiles them before the first frame.
    unsafe {
        let titles: [*const u8; 3] = [
            b"penrose shell\0".as_ptr(),
            b"codeos files\0".as_ptr(),
            b"openweb\0".as_ptr(),
        ];
        for &title in &titles {
            prs_demo_window(title);
        }
        klog("penrose: demos queued\n");
    }

    process_pending(&mut wm);
    klog("penrose: pending done\n");

    // Keep the WindowManager alive in a static so penrose_pump can access it.
    unsafe {
        WM_INSTANCE.replace(wm);
    }
}

fn process_pending(wm: &mut crate::core::WindowManager<CodeOSConn>) {
    let mut count = 0;
    loop {
        match wm.conn_mut().next_event() {
            Ok(evt) => {
                count += 1;
                let _ = wm.process_event(evt);
            }
            Err(_) => break,
        }
    }
    if count > 0 {
        klog(&format_str("penrose: processed ", count));
    }
}

/// Build a small static log line. Avoids `format!`/alloc for the common case.
fn format_str(prefix: &str, n: usize) -> alloc::string::String {
    let mut s = alloc::string::String::from(prefix);
    let mut buf = [0u8; 24];
    let mut x = n;
    let mut i = buf.len();
    if x == 0 {
        buf[i - 1] = b'0';
        i -= 1;
    }
    while x > 0 {
        i -= 1;
        buf[i] = b'0' + (x % 10) as u8;
        x /= 10;
    }
    for &d in &buf[i..buf.len()] {
        s.push(d as char);
    }
    s.push('\n');
    s
}

fn klog(msg: &str) {
    unsafe {
        crate::kprintf(b"%.*s\0".as_ptr(), msg.len() as usize, msg.as_ptr() as usize, 0, 0, 0);
    }
}

/// Process pending WM events and refresh the layout. Called once per frame
/// from the desktop loop, *before* the compositor presents.
///
/// # Safety
/// Must only be called after `penrose_init`.
#[no_mangle]
pub unsafe extern "C" fn penrose_pump() {
    let wm = match WM_INSTANCE.as_mut() {
        Some(w) => w,
        None => return,
    };

    process_pending(wm);
}

static mut WM_INSTANCE: Option<crate::core::WindowManager<CodeOSConn>> = None;

extern "C" {
    fn prs_demo_window(title: *const u8) -> u32;
}
