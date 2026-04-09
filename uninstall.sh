#!/usr/bin/env bash
# vimotion uninstaller — bulletproof, idempotent.
set -Eeuo pipefail
IFS=$'\n\t'

readonly PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly LOG_FILE="${PROJECT_ROOT}/uninstall.log"

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
    exit "${exit_code}"
}
trap 'on_error ${LINENO}' ERR

: > "${LOG_FILE}"
info "========================================"
info "  vimotion - Uninstallation"
info "========================================"
log

if [[ ${EUID} -eq 0 ]]; then
    die "Do not run this script as root. Sudo will be requested when needed."
fi

if ! command -v sudo >/dev/null 2>&1; then
    die "sudo not found — required to remove system files."
fi

#-----------------------------------------------------------------------
# Distribution detection (only used for the closing reminder)
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
    else
        echo unknown
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

#-----------------------------------------------------------------------
# Locate installed files
#-----------------------------------------------------------------------
LIB_PATHS=(
    /usr/lib/fcitx5
    /usr/lib64/fcitx5
    /usr/local/lib/fcitx5
    /usr/local/lib64/fcitx5
    /usr/lib/x86_64-linux-gnu/fcitx5
    /usr/lib/aarch64-linux-gnu/fcitx5
    /usr/local/lib/x86_64-linux-gnu/fcitx5
    /usr/local/lib/aarch64-linux-gnu/fcitx5
)

DATA_FILES=(
    /usr/share/fcitx5/addon/vimotion.conf
    /usr/share/fcitx5/addon/vimotion-module.conf
    /usr/share/fcitx5/inputmethod/vimotion-im.conf
    /usr/local/share/fcitx5/addon/vimotion.conf
    /usr/local/share/fcitx5/addon/vimotion-module.conf
    /usr/local/share/fcitx5/inputmethod/vimotion-im.conf
)

FOUND_FILES=()
for lib_path in "${LIB_PATHS[@]}"; do
    # Current name + legacy names from earlier versions
    for so in vimotion.so vimotion-module.so; do
        [[ -f "${lib_path}/${so}" ]] && FOUND_FILES+=("${lib_path}/${so}")
    done
done
for f in "${DATA_FILES[@]}"; do
    [[ -f "${f}" ]] && FOUND_FILES+=("${f}")
done

# Per-user config (safe to nuke without sudo)
USER_CONFIG_FILES=(
    "${HOME}/.config/fcitx5/conf/vimotion.conf"
    "${HOME}/.config/fcitx5/conf/vimotion-module.conf"
)

USER_CONFIG_PRESENT=0
for f in "${USER_CONFIG_FILES[@]}"; do
    [[ -f "${f}" ]] && USER_CONFIG_PRESENT=1
done

if (( ${#FOUND_FILES[@]} == 0 && USER_CONFIG_PRESENT == 0 )); then
    warn "No installation found. Nothing to uninstall."
    exit 0
fi

if (( ${#FOUND_FILES[@]} > 0 )); then
    warn "Found installed files:"
    for f in "${FOUND_FILES[@]}"; do
        log "  - ${f}"
    done
    log
fi
if (( USER_CONFIG_PRESENT == 1 )); then
    warn "Found user config:"
    for f in "${USER_CONFIG_FILES[@]}"; do
        [[ -f "${f}" ]] && log "  - ${f}"
    done
    log
fi

read -r -p "Remove these files? [y/N] " reply || reply=""
if [[ ! "${reply}" =~ ^[Yy]$ ]]; then
    warn "Uninstallation cancelled."
    exit 0
fi

#-----------------------------------------------------------------------
# Remove
#-----------------------------------------------------------------------
if (( ${#FOUND_FILES[@]} > 0 )); then
    info "Removing system files (sudo)..."
    sudo /usr/bin/rm -f -- "${FOUND_FILES[@]}"
    for f in "${FOUND_FILES[@]}"; do
        ok "  ✓ Removed: ${f}"
    done
fi

for f in "${USER_CONFIG_FILES[@]}"; do
    if [[ -f "${f}" ]]; then
        /usr/bin/rm -f -- "${f}"
        ok "  ✓ Removed: ${f}"
    fi
done
log

#-----------------------------------------------------------------------
# Optional: environment file & autostart cleanup
#-----------------------------------------------------------------------
ENV_FILE="${HOME}/.config/environment.d/fcitx5.conf"
if [[ -f "${ENV_FILE}" ]]; then
    warn "Environment file: ${ENV_FILE}"
    read -r -p "Remove environment configuration? [y/N] " reply || reply=""
    if [[ "${reply}" =~ ^[Yy]$ ]]; then
        /usr/bin/rm -f -- "${ENV_FILE}"
        ok "✓ Environment configuration removed"
        warn "  Logout/login required for the change to apply."
    else
        warn "Keeping environment configuration."
    fi
    log
fi

AUTOSTART_FILES=(
    "${HOME}/.config/autostart/org.fcitx.Fcitx5.desktop"
    "${HOME}/.config/autostart/fcitx5.desktop"
)
for autostart in "${AUTOSTART_FILES[@]}"; do
    if [[ -f "${autostart}" ]]; then
        warn "Autostart file: ${autostart}"
        read -r -p "Remove autostart? [y/N] " reply || reply=""
        if [[ "${reply}" =~ ^[Yy]$ ]]; then
            /usr/bin/rm -f -- "${autostart}"
            ok "✓ Autostart removed"
        else
            warn "Keeping autostart."
        fi
        log
        break
    fi
done

#-----------------------------------------------------------------------
# Restart fcitx5
#-----------------------------------------------------------------------
SESSION="${XDG_SESSION_TYPE:-}"
DESKTOP="${XDG_CURRENT_DESKTOP:-}"

info "Reloading Fcitx5..."
if /usr/bin/pgrep -x fcitx5 >/dev/null 2>&1; then
    if [[ "${SESSION}" == "wayland" && "${DESKTOP}" == "KDE" ]]; then
        if command -v fcitx5-remote >/dev/null 2>&1; then
            fcitx5-remote -r >/dev/null 2>&1 && \
                ok "✓ Fcitx5 config reloaded" || \
                warn "fcitx5-remote -r failed"
        fi
        warn "  Logout/login to fully apply."
    else
        /usr/bin/pkill -x fcitx5 >/dev/null 2>&1 || true
        /usr/bin/sleep 1
        ( /usr/bin/setsid fcitx5 -d >/dev/null 2>&1 & ) || true
        /usr/bin/sleep 2
        if /usr/bin/pgrep -x fcitx5 >/dev/null 2>&1; then
            ok "✓ Fcitx5 restarted"
        else
            warn "Fcitx5 not running."
        fi
    fi
else
    warn "Fcitx5 not running."
fi
log

ok "========================================"
ok "  Uninstallation Complete!"
ok "========================================"
log
warn "Notes:"
case "${DISTRO}" in
    arch)   log "  - Fcitx5 is still installed: sudo pacman -R fcitx5" ;;
    debian) log "  - Fcitx5 is still installed: sudo apt remove fcitx5" ;;
    fedora) log "  - Fcitx5 is still installed: sudo dnf remove fcitx5" ;;
    suse)   log "  - Fcitx5 is still installed: sudo zypper remove fcitx5" ;;
    *)      log "  - Fcitx5 packages remain installed" ;;
esac
log "  - Logout/login to fully apply changes."
log
