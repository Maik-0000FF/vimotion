# vimotion

> **Warning**
> This project is experimental and in an early stage of development. Expect bugs, incomplete features, and breaking changes.

vimotion is a [Fcitx5](https://github.com/fcitx/fcitx5) addon that brings Vim-like modal editing to any application — system-wide. It works by intercepting key events through the input method layer and translating Vim commands into standard system shortcuts.

## Features (v0.1)

- **Three modes**: Normal, Insert, Operator-Pending
- **Motions**: `h` `j` `k` `l` `w` `b` `e` `0` `$` `gg` `G`
- **Operators**: `d` `y` `c` + motion, `dd` `yy` `cc` (line operators)
- **Commands**: `x` `X` `p` `P` `u` `Ctrl+R` `i` `a` `I` `A` `o` `O`
- **Count prefix**: `3j`, `2dd`, `5x`, etc.
- **Terminal detection**: automatic Ctrl+Shift+C/V for terminal emulators
- **Mode indicator**: `[N]` Normal, `[I]` Insert

## Two Variants

vimotion ships as two variants — both are built and installed:

| | Input Method | Module |
|---|---|---|
| **File** | `vimotion.so` | `vimotion-module.so` |
| **Activate** | `Ctrl+Space` (switch IM) | `Ctrl+Escape` (toggle) |
| **Runs alongside other IMs** | no | yes |
| **App blacklist** | no | yes (nvim, vim) |
| **Use when** | vimotion is your only IM | you use vimotion alongside another IM |

### Module variant

The module runs in the background and can be toggled independently of your active input method. This means you can use vimotion together with other Fcitx5 addons (e.g. for special characters or snippets). It automatically disables itself in blacklisted apps like neovim or vim. Each window has its own mode state.

### Input Method variant

A standalone input method that you switch to via the regular Fcitx5 trigger key. Use this if you don't need parallel operation with other input methods.

## Installation

### Requirements

- Fcitx5 and development libraries
- CMake, C++20 compiler

### Quick Install

```bash
./install.sh
```

The install script auto-detects your distribution (Arch, Debian/Ubuntu, Fedora, openSUSE), installs missing dependencies, builds, and installs both variants.

### Manual Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo cmake --install .
```

### Setup

**Module variant (recommended):**
1. Log out and back in
2. vimotion-module loads automatically with Fcitx5
3. Press `Ctrl+Escape` to toggle vimotion on — `[N]` appears
4. Press `i` for Insert Mode `[I]`, `Escape` to return to Normal Mode
5. Press `Ctrl+Escape` again to toggle off

**Input Method variant:**
1. Log out and back in
2. Open `fcitx5-configtool`
3. Go to **Input Method** → click **+** → search **vimotion** → add it
4. Switch to vimotion with your trigger key (default: `Ctrl+Space`)
5. You start in **Normal Mode** `[N]`

### Uninstall

```bash
./uninstall.sh
```

## License

GPL-3.0 — see [LICENSE](LICENSE)
