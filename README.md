# vimotion

> **Warning**
> Experimental and early-stage. Expect bugs, incomplete features, and breaking changes.

vimotion is a [Fcitx5](https://github.com/fcitx/fcitx5) module that brings Vim-like modal editing to any application — system-wide. It intercepts key events through fcitx5 and translates Vim commands into standard system shortcuts.

## Features

- **Three modes**: Normal, Insert, Operator-Pending
- **Motions**: `h` `j` `k` `l` `w` `b` `e` `0` `$` `gg` `G`
- **Operators**: `d` `y` `c` + motion, `dd` `yy` `cc` (line operators)
- **Commands**: `x` `X` `p` `P` `u` `Ctrl+R` `i` `a` `I` `A` `o` `O`
- **Count prefix**: `3j`, `2dd`, `5x`, etc.
- **Terminal detection**: automatic Ctrl+Shift+C/V for terminal emulators
- **Mode indicator**: `[N]` Normal, `[I]` Insert
- **Configurable** through `fcitx5-configtool` → Addons → vimotion:
  - Auto-enable on focus
  - Custom toggle hotkey
  - App white/blacklist
  - Insert-mode key sequence mappings (e.g. `jk` → `Escape`)

## Architecture

vimotion ships as a single Fcitx5 **module** (`vimotion-module.so`). The module runs in the `PreInputMethod` phase and can be toggled per input context, which means:

- It runs alongside any other Fcitx5 input method (no need to switch IM).
- Each window has its own mode state.
- App filtering disables it inside vim/neovim/etc. by default.

## Installation

### Requirements

- Fcitx5 and development libraries
- CMake ≥ 3.16, C++20 compiler

### Quick install

```bash
./install.sh
```

The script auto-detects your distribution (Arch, Debian/Ubuntu, Fedora, openSUSE), installs missing dependencies, builds, runs the test suite, and installs the module. It is **idempotent** — re-running cleans stale installations first.

### Manual build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

### Setup

1. Log out and back in (so the env vars take effect).
2. The module is loaded automatically with Fcitx5.
3. Press `Ctrl+Escape` (default toggle) — `[N]` appears in the input panel.
4. Press `i` for Insert Mode `[I]`, `Escape` (or your `jk` mapping) to return.
5. Press `Ctrl+Escape` again to toggle off.

### Uninstall

```bash
./uninstall.sh
```

## Configuration

Open `fcitx5-configtool` → **Addons** → **vimotion** (or edit `~/.config/fcitx5/conf/vimotion-module.conf` directly).

| Section | Key | Default | Description |
|---|---|---|---|
| `General` | `EnabledByDefault` | `False` | Auto-enable Normal Mode for every new input context |
| `General` | `ToggleKey` | `Control+Escape` | Hotkey list that toggles vimotion on/off |
| `AppFilter` | `Mode` | `Blacklist` | `None`, `Blacklist`, or `Whitelist` |
| `AppFilter` | `Blacklist` | `nvim, vim, neovim` | Substring match against the app's program name |
| `AppFilter` | `Whitelist` | *empty* | Only active in these apps when `Mode=Whitelist` |
| `Mappings` | `TimeoutMs` | `200` | Sequence timeout (50–2000 ms) |
| `Mappings` | `InsertMap` | `jk=Escape` | List of `<sequence>=<keysym>` mappings, applied in Insert Mode |

### Insert-mode key sequences

Like neovim's `inoremap jk <Esc>`, `vimotion` supports multi-key sequences in Insert Mode. Each entry has the form `<sequence>=<key>`:

```
jk=Escape
jj=Return
kj=Escape
```

- The sequence is held only as long as `TimeoutMs`. If the next key isn't part of any mapping, the buffered keys are forwarded as-is and the new key passes through unchanged.
- If the target is `Escape`, vimotion switches to Normal Mode (it does *not* forward Escape to the underlying app).
- Any other target key is forwarded directly.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

70 tests cover toggling, motions, operators, count prefixes, per-IC state, app filters, custom toggle keys, `EnabledByDefault`, and the Insert-mode sequence matcher.

## License

GPL-3.0 — see [LICENSE](LICENSE)
