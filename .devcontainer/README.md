# SimpleOS Dev Container

Use this container as the default development environment when your editor can
open `.devcontainer/` workspaces directly.

Recommended editors:
- Zed
- Cursor
- VS Code

What it gives you:
- cross-compilation toolchain for the i386 kernel build
- GRUB and ISO tooling
- QEMU for local boot testing inside the container
- Node.js for the `web/` demo

Manual fallback:

```bash
./scripts/dev-shell.sh
```

That shell path only works when the Docker CLI is installed and available on
your host.
