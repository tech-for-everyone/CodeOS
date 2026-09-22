# CodeOS Xora applications

This directory contains the compressed `.xora` artifacts generated from the
currently built userspace applications:

```sh
make xora-all
```

The packer reuses manifests from `pkgs/core/<app>/` when available and creates
metadata for legacy binaries that predate manifests. Each archive installs its
binary at the path recorded in `manifest.json`.
