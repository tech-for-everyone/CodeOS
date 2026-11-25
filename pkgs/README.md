# CodeOS packaging tree

This directory holds **everything the build system needs to produce an
installable CodeOS package**: package sources, their descriptors, and the
generated distribution artifacts. It is self-contained — the build rules that
consume it live at the repository root, but no file outside this directory is
part of the package definitions themselves.

## Layout

| Path | Contents |
| --- | --- |
| `core/` | Applications and libraries built from source in this tree. One directory per package, normally with `manifest.json` and `src/`. |
| `extra/` | Additional applications (Android/wine runners, games, tools). |
| `ccp/` | Protocol definitions consumed by core packages (Wayland, XWS). |
| `dev/` | Development toolchain packages (gcc, python, node, ...). |
| `util/` | Utility packages (gzip, tar, openssl, ...). |
| `ports/` | Tracker for turning prebuilt-only packages into source ports. |
| `dist/` | Ready-to-ship distribution artifacts (`.xora`, `.deb`, `.apk`). |
| `xora/` | Generated `.xora` application packages, one per built userspace binary. |
| `tools/gen-registry.py` | Regenerates the package index below. |
| `tools/xora.py` | Read-only `.xora` inspector and validator. |
| `tools/fetch-source.py` | Downloads upstream source for a prebuilt package. |
| `packages.json` | Machine-readable index of every package, descriptor and artifact. |
| `manifest.schema.json` | JSON Schema for `manifest.json` files. |

## Descriptor formats

### `manifest.json` (source packages)

The preferred format. Fields are documented in `manifest.schema.json`.
A minimal example:

```json
{
  "name": "terminal",
  "version": "1.0.0",
  "description": "CodeOS terminal emulator",
  "license": "MIT",
  "type": "source",
  "category": "System",
  "dependencies": ["lib-c", "wm_protocol"],
  "install_path": "/bin/terminal",
  "source": "src/"
}
```

`type` is `source` for a source tree, `prebuilt` for a downloadable tool with
no source in this repository, `binary` for an already-packaged archive
(`format: xora-1`, `archive: tar.gz`, `source: payload/`), and `tool`/`kernel`
for a few special packages. `dependencies` names other packages; `provides`
names virtual capabilities a package satisfies.

### Prebuilt packages

The 32 packages that used to be plain `.ftech` files are now regular package
directories with a `manifest.json`. Twenty-eight still describe unported downloads:

```json
{
  "name": "gcc",
  "version": "12.2.0",
  "description": "GNU C compiler",
  "type": "prebuilt",
  "category": "Development",
  "dependencies": [],
  "source": "https://ftp.gnu.org/gnu/gcc/gcc-12.2.0/gcc-12.2.0.tar.xz",
  "size": 52428800,
  "format": "ftech",
  "port_status": "not-started",
  "note": "Converted from dev/gcc.ftech."
}
```

`size` is the original declared uncompressed payload size in bytes. `source`
now carries the upstream source URL so the package can be ported. `.ftech`
files have been removed; `manifest.json` is the only descriptor format.

`gzip`, `tar`, `zip` and `unzip` have been ported: their manifests are now
`type: source` with `source: src/`, `port_status: ported`, an `install_path`,
and an `upstream` field holding the original URL. Each has real source and
host tests under its package directory. `extra/micro/` is a new editor package
requested alongside them, also with source and host tests. See
`ports/README.md` for the remaining work.

### Legacy packages (no descriptor)

`packages.json` marks directories without a descriptor as
`"manifest_status": "legacy"`. There are currently none: every source directory
has a `manifest.json`. Packages whose metadata was originally synthesized by
the packer now carry a source manifest with the same `install_path` (and an
empty `dependencies` list, to preserve the original behavior).

## Artifact formats

### `.xora`

A gzip-compressed tar archive (`format: xora-1`) containing:

```text
manifest.json          # binary manifest (type=binary, source=payload/)
payload/bin/<name>     # installed at the path recorded in install_path
```

See `xora/README.md`. Artifacts are named `<name>-<version>.xora`.

Archives are read-only in this tree. Use `python3 pkgs/tools/xora.py verify`
to check every archive's structure and `... inspect <archive>` to view its
manifest and payload members; neither command modifies the files.

### `.deb` / `.apk`

Alternative distribution formats for the same software, currently produced for
`netbeam` in `dist/`. They carry an architecture suffix where needed, e.g.
`netbeam-1.0.0_amd64.deb`.

## Adding a package

1. Create `core/<name>/` (or `extra/<name>/`) with a `manifest.json` and `src/`.
2. Use a lowercase, hyphenated `name` with no spaces; keep it equal to the
   directory name.
3. Set `install_path` to the absolute destination of the built payload.
4. List only dependencies that are real package names (see `packages.json`).
5. From the repository root, run `make xora-all` to generate `xora/<name>-<version>.xora`.
6. Regenerate the index: `python3 pkgs/tools/gen-registry.py`.

## Maintaining the index

`packages.json` is generated, not hand-edited. It records, for every package:

- `path` / `descriptor` — relative to this directory;
- `manifest_status` — `explicit` (all packages) or `legacy` if a descriptor is missing;
- `artifacts` — built files that belong to the package;
- the descriptor metadata (version, description, license, dependencies, ...).

The generator resolves the special case where `xora/compositor-*.xora` is
produced by `core/panels` through an explicit alias. Keep that alias table in
`tools/gen-registry.py` up to date if more binaries get renamed.

## Known inconsistencies

These are tracked here so they can be cleaned up deliberately rather than
silently:

- `extra/netbeam/manifest.json` is a *binary* manifest while the package also
  ships source, so its `type`/`source` fields do not follow the source-package
  convention.
- `core/openweb/manifest.json` uses a `categories` array instead of the single
  `category` string used by most manifests. Both are accepted; prefer
  `category` in new packages.
- Of the 32 converted packages, 28 still have no source tree here and are
  `port_status: not-started`; `gzip`, `tar`, `zip` and `unzip` now have source
  ports with host tests, plus the new `micro` editor. See `ports/README.md`.
- The on-device fetcher still expects `<name>-<version>.ftech` payloads in its
  cache. The descriptor files in this tree are now JSON, so the fetcher and the
  root build rules will need updating before prebuilt downloads resume. That
  code lives outside `pkgs/` and was not changed here.
