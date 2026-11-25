# CodeOS Xora applications

This directory contains the compressed `.xora` artifacts generated from the
currently built userspace applications:

```sh
make xora-all
```

The packer reuses manifests from `pkgs/core/<app>/` (and other package
directories) when available and creates metadata for legacy binaries that
predate manifests. Each archive installs its binary at the path recorded in
`manifest.json`.

## Archive format

Each `.xora` is a gzip-compressed tar with two parts:

```text
manifest.json          # binary manifest: format=xora-1, type=binary
payload/<install_path> # e.g. payload/bin/terminal
```

## Working with archives

Archives in this tree are treated as read-only build outputs. To inspect or
check them without modifying anything:

```sh
python3 pkgs/tools/xora.py list
python3 pkgs/tools/xora.py inspect pkgs/xora/terminal-1.0.0.xora
python3 pkgs/tools/xora.py verify       # checks all archives under xora/ and dist/
```

`verify` checks that the archive is a readable gzip tar, that its manifest has
the required fields (`name`, `version`, `format`, `type`, `install_path`), that
`format`/`archive` are `xora-1`/`tar.gz`, and that a payload member exists at
`payload/<install_path>`. It does not rewrite or repack anything.

For a complete, machine-readable view of sources, descriptors and artifacts,
see `pkgs/packages.json` and the layout guide in `pkgs/README.md`.
