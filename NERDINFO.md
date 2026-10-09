# HyperDE architecture

`src/backend/chroma` contains the X11 compositor connection. It owns
`_NET_WM_CM_S<n>`, requests Composite manual redirection, and consumes X11
events.

`src/backend/hwms` contains the window manager entry point. It creates
Penrose's `RustConn`, which is the x11rb implementation of Penrose's connection
trait, and starts `WindowManager`.

The modules deliberately do not import `libc`, `nix`, Wayland, wlroots, or
Linux device APIs.
