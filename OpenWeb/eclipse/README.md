# OpenWeb Eclipse / Gecko boundary

This directory is the host-side integration surface for the future Gecko
port. Eclipse owns the Gecko process and event loop, then installs its script
evaluator, page loader, event pump, and shutdown callback with
`openweb_gecko_adapter_install()`.

After installation, OpenWeb uses `openweb_gecko_adapter_start()` for page
loads and `openweb_gecko_adapter_pump()` from the host event loop. The page
loader returns serialized HTML to the existing OpenWeb renderer while Gecko
script execution is routed into Jengine's selected host backend.

The CodeOS kernel and the small Jengine evaluator do not claim to be Gecko.
Until a verified Gecko host is installed, selecting the Gecko backend returns
`JENGINE_BACKEND_UNAVAILABLE` and OpenWeb remains on its litehtml/text path.
