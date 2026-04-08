# vimotion

> **Warning**
> This project is experimental and in an early stage of development. Expect bugs, incomplete features, and breaking changes.

vimotion is a [Fcitx5](https://github.com/fcitx/fcitx5) input method addon that brings Vim-like modal editing to any application — system-wide. It works by intercepting key events through the input method layer and translating Vim commands into standard system shortcuts.

## Features (v0.1)

- **Three modes**: Normal, Insert, Operator-Pending
- **Motions**: `h` `j` `k` `l` `w` `b` `e` `0` `$` `gg` `G`
- **Operators**: `d` `y` `c` + motion, `dd` `yy` `cc` (line operators)
- **Commands**: `x` `X` `p` `P` `u` `Ctrl+R` `i` `a` `I` `A` `o` `O`
- **Count prefix**: `3j`, `2dd`, `5x`, etc.
- **Terminal detection**: automatic Ctrl+Shift+C/V for terminal emulators
- **Mode indicator**: `[N]` Normal, `[I]` Insert

## Installation

### Requirements

- Fcitx5 and development libraries
- CMake, C++20 compiler

### Quick Install

```bash
./install.sh
```

The install script auto-detects your distribution (Arch, Debian/Ubuntu, Fedora, openSUSE), installs missing dependencies, builds, and installs the addon.

### Manual Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo cmake --install .
```

### Setup

1. Log out and back in (for environment variables)
2. Open `fcitx5-configtool`
3. Go to **Input Method** → click **+** → search **vimotion** → add it
4. Switch to vimotion with your trigger key (default: `Ctrl+Space`)
5. You start in **Normal Mode** `[N]` — press `i` for Insert Mode `[I]`, `Escape` to return

### Uninstall

```bash
./uninstall.sh
```

## License

GPL-3.0 — see [LICENSE](LICENSE)
