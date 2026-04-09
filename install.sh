#!/usr/bin/env bash
# vimotion installer — bulletproof, idempotent, distro-aware.
#
# Strict mode:
#   -E  ERR trap propagates into subshells/functions
#   -e  exit on any non-zero return
#   -u  unset variables are errors
#   -o pipefail  any failure in a pipeline fails the whole pipe
set -Eeuo pipefail
IFS=$'\n\t'

#-----------------------------------------------------------------------
# Constants & helpers
#-----------------------------------------------------------------------
readonly PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly BUILD_DIR="${PROJECT_ROOT}/build"
readonly LOG_FILE="${PROJECT_ROOT}/install.log"

readonly RED=$'\033[0;31m'
readonly GREEN=$'\033[0;32m'
readonly YELLOW=$'\033[1;33m'
readonly BLUE=$'\033[0;34m'
readonly NC=$'\033[0m'

log()  { printf '%s\n' "$*" | /usr/bin/tee -a "${LOG_FILE}" >&2; }
info() { log "${BLUE}$*${NC}"; }
ok()   { log "${GREEN}$*${NC}"; }
warn() { log "${YELLOW}$*${NC}"; }
err()  { log "${RED}$*${NC}"; }

die() {
    err "Error: $*"
    err "See ${LOG_FILE} for details."
    exit 1
}

on_error() {
    local exit_code=$?
    local line_no=$1
    err "Aborted at line ${line_no} (exit ${exit_code})."
    err "See ${LOG_FILE} for details."
    exit "${exit_code}"
}
trap 'on_error ${LINENO}' ERR

require_cmd() {
    local cmd
    for cmd in "$@"; do
        command -v "${cmd}" >/dev/null 2>&1 \
            || die "Required command not found: ${cmd}"
    done
}

#-----------------------------------------------------------------------
# Header
#-----------------------------------------------------------------------
: > "${LOG_FILE}"
info "========================================"
info "  vimotion - Installation"
info "========================================"
log

#-----------------------------------------------------------------------
# Sanity checks
#-----------------------------------------------------------------------
if [[ ${EUID} -eq 0 ]]; then
    die "Do not run this script as root. Sudo will be requested when needed."
fi

require_cmd /usr/bin/uname /usr/bin/tee
# sudo is required for installing files into system directories
require_cmd sudo

#-----------------------------------------------------------------------
# Distribution detection
#-----------------------------------------------------------------------
detect_distro() {
    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        case "${ID:-}" in
            arch|manjaro|endeavouros|garuda|artix|cachyos) echo arch ;;
            debian|ubuntu|linuxmint|pop|kali|elementary|zorin|mx|neon)
                echo debian ;;
            fedora|nobara) echo fedora ;;
            opensuse*|suse) echo suse ;;
            *)
                case "${ID_LIKE:-}" in
                    *arch*) echo arch ;;
                    *debian*|*ubuntu*) echo debian ;;
                    *fedora*) echo fedora ;;
                    *suse*) echo suse ;;
                    *) echo unknown ;;
                esac ;;
        esac
    elif command -v pacman >/dev/null 2>&1; then echo arch
    elif command -v apt    >/dev/null 2>&1; then echo debian
    elif command -v dnf    >/dev/null 2>&1; then echo fedora
    elif command -v zypper >/dev/null 2>&1; then echo suse
    else echo unknown
    fi
}

readonly DISTRO="$(detect_distro)"

if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    DISTRO_NAME="${PRETTY_NAME:-${ID:-Unknown}}"
else
    DISTRO_NAME="Unknown"
fi
readonly DISTRO_NAME

info "Distribution: ${DISTRO_NAME} (${DISTRO})"
log

case "${DISTRO}" in
    arch|debian|fedora|suse) ;;
    *)
        err "Unsupported distribution: ${DISTRO_NAME}"
        log
        warn "Supported families: Arch, Debian/Ubuntu, Fedora, openSUSE."
        log
        log "Manual build:"
        log "  /usr/bin/cmake -S \"${PROJECT_ROOT}\" -B \"${BUILD_DIR}\""
        log "  /usr/bin/cmake --build \"${BUILD_DIR}\" -j"
        log "  sudo /usr/bin/cmake --install \"${BUILD_DIR}\""
        exit 1
        ;;
esac

#-----------------------------------------------------------------------
# Dependencies
#-----------------------------------------------------------------------
is_installed() {
    case "${DISTRO}" in
        arch)        /usr/bin/pacman -Q "$1" >/dev/null 2>&1 ;;
        debian)      /usr/bin/dpkg -l "$1" 2>/dev/null \
                        | /usr/bin/grep -q '^ii' ;;
        fedora|suse) /usr/bin/rpm -q "$1" >/dev/null 2>&1 ;;
    esac
}

install_deps() {
    case "${DISTRO}" in
        arch)
            sudo /usr/bin/pacman -S --needed --noconfirm "$@"
            ;;
        debian)
            info "Updating package list..."
            sudo /usr/bin/apt-get update
            sudo /usr/bin/apt-get install -y --no-install-recommends "$@"
            ;;
        fedora)
            sudo /usr/bin/dnf install -y "$@"
            ;;
        suse)
            sudo /usr/bin/zypper --non-interactive install "$@"
            ;;
    esac
}

case "${DISTRO}" in
    arch)
        DEPS=(fcitx5 fcitx5-configtool fcitx5-qt fcitx5-gtk cmake gcc) ;;
    debian)
        DEPS=(fcitx5 fcitx5-config-qt fcitx5-frontend-gtk3
              fcitx5-frontend-gtk4 fcitx5-frontend-qt5 libfcitx5core-dev
              fcitx5-modules-dev cmake g++) ;;
    fedora)
        DEPS=(fcitx5 fcitx5-configtool fcitx5-gtk fcitx5-qt6
              fcitx5-devel cmake gcc-c++) ;;
    suse)
        DEPS=(fcitx5 fcitx5-configtool fcitx5-gtk fcitx5-qt6
              fcitx5-devel cmake gcc-c++) ;;
esac

info "Checking dependencies..."
MISSING_DEPS=()
for dep in "${DEPS[@]}"; do
    if is_installed "${dep}"; then
        log "  ${GREEN}✓${NC} ${dep}"
    else
        log "  ${RED}✗${NC} ${dep} (missing)"
        MISSING_DEPS+=("${dep}")
    fi
done
log

if (( ${#MISSING_DEPS[@]} > 0 )); then
    warn "Missing: ${MISSING_DEPS[*]}"
    read -r -p "Install missing dependencies? [Y/n] " reply || reply=""
    if [[ ! "${reply}" =~ ^[Nn]$ ]]; then
        install_deps "${MISSING_DEPS[@]}"
        ok "✓ Dependencies installed"
    else
        die "Cannot proceed without dependencies."
    fi
else
    ok "✓ All dependencies present"
fi
log

#-----------------------------------------------------------------------
# Verify required tools after dependency install
#-----------------------------------------------------------------------
require_cmd /usr/bin/cmake
if command -v /usr/bin/c++ >/dev/null 2>&1; then
    : # OK
elif command -v /usr/bin/g++ >/dev/null 2>&1; then
    : # OK
else
    die "No C++ compiler (c++/g++) found in /usr/bin."
fi

#-----------------------------------------------------------------------
# Stale installation detection (BEFORE building, so we can warn early)
#-----------------------------------------------------------------------
STALE_LIB_CANDIDATES=(
    /usr/lib/fcitx5/vimotion.so
    /usr/lib/fcitx5/vimotion-module.so
    /usr/lib64/fcitx5/vimotion.so
    /usr/lib64/fcitx5/vimotion-module.so
    /usr/local/lib/fcitx5/vimotion.so
    /usr/local/lib/fcitx5/vimotion-module.so
    /usr/local/lib64/fcitx5/vimotion.so
    /usr/local/lib64/fcitx5/vimotion-module.so
)
STALE_DATA_CANDIDATES=(
    /usr/share/fcitx5/addon/vimotion.conf
    /usr/share/fcitx5/addon/vimotion-module.conf
    /usr/share/fcitx5/inputmethod/vimotion-im.conf
    /usr/local/share/fcitx5/addon/vimotion.conf
    /usr/local/share/fcitx5/addon/vimotion-module.conf
    /usr/local/share/fcitx5/inputmethod/vimotion-im.conf
)

if [[ "${DISTRO}" == "debian" ]]; then
    STALE_LIB_CANDIDATES+=(
        /usr/lib/x86_64-linux-gnu/fcitx5/vimotion.so
        /usr/lib/x86_64-linux-gnu/fcitx5/vimotion-module.so
        /usr/lib/aarch64-linux-gnu/fcitx5/vimotion.so
        /usr/lib/aarch64-linux-gnu/fcitx5/vimotion-module.so
        /usr/local/lib/x86_64-linux-gnu/fcitx5/vimotion.so
        /usr/local/lib/x86_64-linux-gnu/fcitx5/vimotion-module.so
        /usr/local/lib/aarch64-linux-gnu/fcitx5/vimotion.so
        /usr/local/lib/aarch64-linux-gnu/fcitx5/vimotion-module.so
    )
fi

STALE_FILES=()
for f in "${STALE_LIB_CANDIDATES[@]}" "${STALE_DATA_CANDIDATES[@]}"; do
    [[ -f "${f}" ]] && STALE_FILES+=("${f}")
done

if (( ${#STALE_FILES[@]} > 0 )); then
    warn "Found previous installation files:"
    for f in "${STALE_FILES[@]}"; do
        log "  - ${f}"
    done
    log
    read -r -p "Remove before reinstalling? [Y/n] " reply || reply=""
    if [[ ! "${reply}" =~ ^[Nn]$ ]]; then
        sudo /usr/bin/rm -f -- "${STALE_FILES[@]}"
        ok "✓ Old installation removed"
    else
        warn "Old files may conflict with the new installation"
    fi
    log
fi

#-----------------------------------------------------------------------
# Build (out-of-source, idempotent: rm -rf build/ first)
#-----------------------------------------------------------------------
info "Building vimotion..."
if [[ -e "${BUILD_DIR}" && ! -d "${BUILD_DIR}" ]]; then
    die "${BUILD_DIR} exists but is not a directory."
fi
/usr/bin/rm -rf -- "${BUILD_DIR}"
/usr/bin/mkdir -p -- "${BUILD_DIR}"

/usr/bin/cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release
/usr/bin/cmake --build "${BUILD_DIR}" -j "$(/usr/bin/nproc)"
ok "✓ Build successful"
log

#-----------------------------------------------------------------------
# Tests (best-effort)
#-----------------------------------------------------------------------
info "Running tests..."
if (cd "${BUILD_DIR}" && /usr/bin/ctest --output-on-failure); then
    ok "✓ Tests passed"
else
    warn "Tests reported failures — continuing anyway."
fi
log

#-----------------------------------------------------------------------
# Install
#-----------------------------------------------------------------------
info "Installing vimotion (sudo)..."
sudo /usr/bin/cmake --install "${BUILD_DIR}"
ok "✓ Module installed"
log

#-----------------------------------------------------------------------
# Verify install actually placed the .so somewhere fcitx will find it
#-----------------------------------------------------------------------
INSTALLED=()
for f in "${STALE_LIB_CANDIDATES[@]}"; do
    [[ -f "${f}" ]] && INSTALLED+=("${f}")
done
if (( ${#INSTALLED[@]} == 0 )); then
    die "Install completed but vimotion-module.so was not found in any known fcitx5 addon directory."
fi
ok "Installed library:"
for f in "${INSTALLED[@]}"; do
    log "  - ${f}"
done
log

#-----------------------------------------------------------------------
# Environment variables
#-----------------------------------------------------------------------
ENV_FILE="${HOME}/.config/environment.d/fcitx5.conf"
info "Configuring environment variables..."
/usr/bin/mkdir -p -- "${HOME}/.config/environment.d"

write_env_file() {
    /usr/bin/tee "${ENV_FILE}" >/dev/null <<'EOF'
GTK_IM_MODULE=fcitx
QT_IM_MODULE=fcitx
XMODIFIERS=@im=fcitx
GLFW_IM_MODULE=ibus
EOF
    ok "✓ Environment variables configured"
}

if [[ -f "${ENV_FILE}" ]]; then
    warn "Environment file already exists: ${ENV_FILE}"
    log "Current contents:"
    /usr/bin/sed 's/^/    /' "${ENV_FILE}" | /usr/bin/tee -a "${LOG_FILE}" >&2
    read -r -p "Overwrite with fcitx5 settings? [Y/n] " reply || reply=""
    if [[ "${reply}" =~ ^[Nn]$ ]]; then
        warn "Skipped environment setup."
    else
        write_env_file
    fi
else
    write_env_file
fi
log

#-----------------------------------------------------------------------
# Debian: im-config
#-----------------------------------------------------------------------
if [[ "${DISTRO}" == "debian" ]]; then
    info "Configuring input method framework..."
    if ! command -v im-config >/dev/null 2>&1; then
        warn "Installing im-config..."
        sudo /usr/bin/apt-get install -y im-config
    fi
    im-config -n fcitx5 >/dev/null 2>&1 || true
    ok "✓ Fcitx5 set as default input method"
    log
fi

#-----------------------------------------------------------------------
# Autostart handling (KDE-Wayland disables duplicate autostart)
#-----------------------------------------------------------------------
SESSION="${XDG_SESSION_TYPE:-}"
DESKTOP="${XDG_CURRENT_DESKTOP:-}"

if [[ "${SESSION}" == "wayland" && "${DESKTOP}" == "KDE" ]]; then
    AUTOSTART_FILE="${HOME}/.config/autostart/org.fcitx.Fcitx5.desktop"
    if [[ ! -f "${AUTOSTART_FILE}" ]] || \
       ! /usr/bin/grep -q '^Hidden=true' "${AUTOSTART_FILE}"; then
        info "Disabling redundant fcitx5 autostart (KWin handles this)..."
        /usr/bin/mkdir -p -- "${HOME}/.config/autostart"
        /usr/bin/tee "${AUTOSTART_FILE}" >/dev/null <<'EOF'
[Desktop Entry]
Hidden=true
EOF
        ok "✓ Duplicate autostart disabled"
    fi
elif [[ "${DISTRO}" != "arch" ]]; then
    info "Setting up autostart..."
    AUTOSTART_DIR="${HOME}/.config/autostart"
    /usr/bin/mkdir -p -- "${AUTOSTART_DIR}"
    if   [[ -f /usr/share/applications/org.fcitx.Fcitx5.desktop ]]; then
        /usr/bin/cp -- /usr/share/applications/org.fcitx.Fcitx5.desktop \
                       "${AUTOSTART_DIR}/"
        ok "✓ Fcitx5 will autostart on login"
    elif [[ -f /usr/share/applications/fcitx5.desktop ]]; then
        /usr/bin/cp -- /usr/share/applications/fcitx5.desktop "${AUTOSTART_DIR}/"
        ok "✓ Fcitx5 will autostart on login"
    else
        warn "Could not find Fcitx5 desktop file for autostart"
    fi
fi
log

#-----------------------------------------------------------------------
# Restart fcitx5
#-----------------------------------------------------------------------
info "Reloading Fcitx5..."
if /usr/bin/pgrep -x fcitx5 >/dev/null 2>&1; then
    if [[ "${SESSION}" == "wayland" && "${DESKTOP}" == "KDE" ]]; then
        if command -v fcitx5-remote >/dev/null 2>&1; then
            fcitx5-remote -r >/dev/null 2>&1 && \
                ok "✓ Fcitx5 config reloaded" || \
                warn "fcitx5-remote -r failed"
        fi
        warn "  Right-click the tray icon → Exit, KWin will restart it."
    else
        /usr/bin/pkill -x fcitx5 >/dev/null 2>&1 || true
        /usr/bin/sleep 1
        ( /usr/bin/setsid fcitx5 -d >/dev/null 2>&1 & ) || true
        /usr/bin/sleep 2
        if /usr/bin/pgrep -x fcitx5 >/dev/null 2>&1; then
            ok "✓ Fcitx5 restarted"
        else
            warn "Fcitx5 not running yet — start it manually or relogin."
        fi
    fi
else
    warn "Fcitx5 not running — it will start on next login."
fi
log

#-----------------------------------------------------------------------
# Final instructions
#-----------------------------------------------------------------------
ok "========================================"
ok "  Installation Complete!"
ok "========================================"
log
warn "Next steps:"
log
log "1. ${RED}LOG OUT and back IN${NC} so the env vars take effect."
log
log "2. The vimotion module is loaded automatically with Fcitx5."
log "   Default toggle: ${BLUE}Ctrl+Escape${NC}"
log "   Configure via:  ${BLUE}fcitx5-configtool${NC} → Addons → vimotion"
log
log "3. In Normal Mode [N]:"
log "     h j k l   move cursor"
log "     w b e     word motions"
log "     i a I A   enter Insert Mode [I]"
log "     dd yy p   delete / yank / paste"
log "     gg G      document start / end"
log "     3j 5x     count prefix"
log
log "4. To return to Normal Mode from Insert Mode, press ${BLUE}Escape${NC}"
log "   or use a configured mapping like ${BLUE}jk${NC}."
log
warn "Troubleshooting:"
log "  - fcitx5-diagnose"
log "  - tail -f ${LOG_FILE}"
log "  - README.md"
log
