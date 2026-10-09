# gnustep-gui build recipe (vendored)

`ace-host` Stage 3b links GNUstep's GUI class library. The distro/AUR
`gnustep-gui` 0.32.0 release does not define `CALL_NON_NULL_BLOCK` in
`Headers/AppKit/AppKitDefines.h`, and the current toolchain needs it, so the
`build()` function of this recipe injects it with a `sed` before `make`.
Without that line the class library does not build.

This is a verbatim copy of the AUR recipe
(<https://aur.archlinux.org/gnustep-gui.git>) at 0.32.0 plus that patch, kept
here so the host dependency is reproducible.

## Build

```sh
cp -r ace-host/deps/gnustep-gui /tmp/gnustep-gui-build
cd /tmp/gnustep-gui-build
makepkg -si            # needs gnustep-make, gnustep-base, gcc-objc; installs as root
```

`gnustep-back` (the drawing backend) and `gnustep-base` come from the distro's
normal packages; only the class library needed the rebuild. The working clone
and its build output live in the repo root as `gnustep-gui/`, which is
gitignored -- only this recipe is tracked.
