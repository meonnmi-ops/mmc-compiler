#!/bin/bash
# =================================================================
#  mmcd.sh  —  MMC Daemon (Termux Background Compiler Service)
# =================================================================
#  Phase 7: Daemonization
#
#  Usage:
#      bash mmcd.sh start       # Background daemon စတင်မယ်
#      bash mmcd.sh stop        # Daemon ရပ်မယ်
#      bash mmcd.sh status      # အခြေအနေ ကြည့်မယ်
#      bash mmcd.sh foreground  # Debug mode (terminal မှာ ပြသမယ်)
#
#  Termux Paths:
#      PID  : $PREFIX/tmp/mmcd.pid
#      Log  : $PREFIX/tmp/mmcd.log
#
#  Target: Android Termux / ARMv8-A / clang + lld
#  Version: 1.1.0
# =================================================================

set -euo pipefail

# ── Termux-compatible paths ──
MMC_HOME="${MMC_HOME:-$HOME/z/my-project/mmc-compiler}"
BUILD_DIR="$MMC_HOME/build"
SELFHOSTED="$MMC_HOME/selfhosted"
WATCH_DIR="$SELFHOSTED"

# Termux: $PREFIX = /data/data/com.termux/files/usr
PID_FILE="${PREFIX:-/data/data/com.termux/files/usr}/tmp/mmcd.pid"
LOG_FILE="${PREFIX:-/data/data/com.termux/files/usr}/tmp/mmcd.log"
INTERVAL="${MMC_WATCH_INTERVAL:-10}"

# ── Compiler settings ──
CC="${MMC_CC:-clang}"
CFLAGS="${MMC_CFLAGS:--std=c99 -Wall -O3 -march=armv8-a}"
LDFLAGS="${MMC_LDFLAGS:--lm}"

# Runtime objects (pre-built by bootstrap.sh)
RUNTIME_OBJS="$BUILD_DIR/mmclib.o $BUILD_DIR/ia_bridge.o"

# ── Colors ──
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; NC='\033[0m'

log()   { echo "[$(date '+%H:%M:%S')] $1" >> "$LOG_FILE"; }
log_ok()  { echo "[$(date '+%H:%M:%S')] [OK] $1" >> "$LOG_FILE"; }
log_err() { echo "[$(date '+%H:%M:%S')] [ERROR] $1" >> "$LOG_FILE"; }

# ── Daemon Core ──

daemon_loop() {
    log "mmcd daemon started. Watching: $WATCH_DIR"

    while true; do
        # Scan for .mmc files
        for mmc_file in "$WATCH_DIR"/*.mmc; do
            [ -f "$mmc_file" ] || continue

            local base
            base=$(basename "$mmc_file" .mmc)
            local bin_file="$BUILD_DIR/$base"
            local c_file="$BUILD_DIR/${base}.c"

            # Skip if binary exists and is newer than source
            if [ -f "$bin_file" ]; then
                local src_time bin_time
                src_time=$(stat -c %Y "$mmc_file" 2>/dev/null || stat -f %m "$mmc_file" 2>/dev/null)
                bin_time=$(stat -c %Y "$bin_file" 2>/dev/null || stat -f %m "$bin_file" 2>/dev/null)
                [ "$bin_time" -ge "$src_time" ] 2>/dev/null && continue
            fi

            # Skip library modules (no main function)
            case "$base" in
                mmc_lexer|mmc_parser|mmc_codegen|mmc_c_codegen|test_*|*_test)
                    continue
                    ;;
            esac

            log "Compiling: $base.mmc"

            # MMC -> C
            python3 "$MMC_HOME/compile_mmc.py" "$mmc_file" --c 2>/dev/null | \
                sed '1,/^Generated C Code:/d' | sed '/^===/d' | sed '/^$/d' > "$c_file" 2>>"$LOG_FILE"

            [ -s "$c_file" ] || continue

            # Check for main() in generated C
            if ! grep -q 'int main' "$c_file"; then
                log "$base.mmc: no main() — skipped (library module)"
                continue
            fi

            # C -> Binary
            $CC $CFLAGS "$c_file" $RUNTIME_OBJS $LDFLAGS \
                -o "$bin_file" -I"$SELFHOSTED" 2>>"$LOG_FILE"

            if [ -f "$bin_file" ]; then
                local size
                size=$(ls -lh "$bin_file" | awk '{print $5}')
                log_ok "$base.mmc -> $bin_file ($size)"
            else
                log_err "$base.mmc: compile failed"
            fi
        done

        sleep "$INTERVAL"
    done
}

# ── Commands ──

case "${1:-}" in
    start)
        # Already running?
        if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
            echo -e "${YELLOW}mmcd အသင့်ရှိနေပါပြီ (PID: $(cat "$PID_FILE"))${NC}"
            exit 0
        fi

        mkdir -p "$BUILD_DIR" "$(dirname "$PID_FILE")"

        # Start in background
        nohup bash -c "
            MMC_HOME='$MMC_HOME'
            BUILD_DIR='$BUILD_DIR'
            SELFHOSTED='$SELFHOSTED'
            WATCH_DIR='$WATCH_DIR'
            CC='$CC'
            CFLAGS='$CFLAGS'
            LDFLAGS='$LDFLAGS'
            INTERVAL='$INTERVAL'
            PID_FILE='$PID_FILE'
            LOG_FILE='$LOG_FILE'
            RUNTIME_OBJS='$RUNTIME_OBJS'

            log()   { echo \"[\$(date '+%H:%M:%S')] \$1\" >> \"\$LOG_FILE\"; }
            log_ok()  { echo \"[\$(date '+%H:%M:%S')] [OK] \$1\" >> \"\$LOG_FILE\"; }
            log_err() { echo \"[\$(date '+%H:%M:%S')] [ERROR] \$1\" >> \"\$LOG_FILE\"; }

            trap 'rm -f \"\$PID_FILE\"; log \"mmcd stopped\"; exit 0' SIGTERM SIGINT

            daemon_loop() {
                log \"mmcd daemon started. Watching: \$WATCH_DIR\"
                while true; do
                    for mmc_file in \"\$WATCH_DIR\"/*.mmc; do
                        [ -f \"\$mmc_file\" ] || continue
                        local base; base=\$(basename \"\$mmc_file\" .mmc)
                        local bin_file=\"\$BUILD_DIR/\$base\"
                        local c_file=\"\$BUILD_DIR/\${base}.c\"
                        if [ -f \"\$bin_file\" ]; then
                            local src_time bin_time
                            src_time=\$(stat -c %Y \"\$mmc_file\" 2>/dev/null || stat -f %m \"\$mmc_file\" 2>/dev/null)
                            bin_time=\$(stat -c %Y \"\$bin_file\" 2>/dev/null || stat -f %m \"\$bin_file\" 2>/dev/null)
                            [ \"\$bin_time\" -ge \"\$src_time\" ] 2>/dev/null && continue
                        fi
                        case \"\$base\" in mmc_lexer|mmc_parser|mmc_codegen|mmc_c_codegen|test_*|*_test) continue ;; esac
                        log \"Compiling: \$base.mmc\"
                        python3 \"\$MMC_HOME/compile_mmc.py\" \"\$mmc_file\" --c 2>/dev/null | sed '1,/^Generated C Code:/d' | sed '/^===/d' | sed '/^$/d' > \"\$c_file\" 2>>\"\$LOG_FILE\"
                        [ -s \"\$c_file\" ] || continue
                        if ! grep -q 'int main' \"\$c_file\"; then
                            log \"\$base.mmc: no main() — skipped\"
                            continue
                        fi
                        \$CC \$CFLAGS \"\$c_file\" \$RUNTIME_OBJS \$LDFLAGS -o \"\$bin_file\" -I\"\$SELFHOSTED\" 2>>\"\$LOG_FILE\"
                        if [ -f \"\$bin_file\" ]; then
                            local size; size=\$(ls -lh \"\$bin_file\" | awk '{print \$5}')
                            log_ok \"\$base.mmc -> \$bin_file (\$size)\"
                        else
                            log_err \"\$base.mmc: compile failed\"
                        fi
                    done
                    sleep \"\$INTERVAL\"
                done
            }
            daemon_loop
        " > "$LOG_FILE" 2>&1 &

        echo $! > "$PID_FILE"
        sleep 1

        if kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
            echo -e "${GREEN}✅ MMC Daemon started (PID: $(cat $PID_FILE))${NC}"
            echo "   Log: $LOG_FILE"
            echo "   Watch: $WATCH_DIR (every ${INTERVAL}s)"
        else
            echo -e "${RED}❌ MMC Daemon failed to start${NC}"
            rm -f "$PID_FILE"
            exit 1
        fi
        ;;

    stop)
        if [ -f "$PID_FILE" ]; then
            local pid
            pid=$(cat "$PID_FILE")
            if kill -0 "$pid" 2>/dev/null; then
                kill "$pid" 2>/dev/null
                sleep 1
                # Force kill if still running
                kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null
            fi
            rm -f "$PID_FILE"
            echo -e "${GREEN}🛑 MMC Daemon stopped${NC}"
        else
            echo -e "${YELLOW}mmcd မစတင်ထားပါဘူး${NC}"
        fi
        ;;

    status)
        if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
            echo -e "${GREEN}🟢 Running (PID: $(cat $PID_FILE))${NC}"
            [ -f "$LOG_FILE" ] && echo "   Log: $LOG_FILE"
            [ -f "$LOG_FILE" ] && echo "   Last entries:" && tail -5 "$LOG_FILE" | sed 's/^/   /'
        else
            echo -e "${RED}🔴 Stopped${NC}"
            [ -f "$PID_FILE" ] && rm -f "$PID_FILE"
        fi
        ;;

    foreground)
        echo -e "${CYAN}MMC Daemon — foreground mode (Ctrl+C to stop)${NC}"
        echo "   Watching: $WATCH_DIR (every ${INTERVAL}s)"
        trap 'echo -e "\n🛑 Daemon stopped"; exit 0' SIGINT SIGTERM
        mkdir -p "$BUILD_DIR"
        # Source variables for daemon_loop
        MMC_HOME="$MMC_HOME" BUILD_DIR="$BUILD_DIR" SELFHOSTED="$SELFHOSTED" \
        WATCH_DIR="$WATCH_DIR" CC="$CC" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" \
        INTERVAL="$INTERVAL" PID_FILE="$PID_FILE" LOG_FILE="$LOG_FILE" \
        RUNTIME_OBJS="$RUNTIME_OBJS" daemon_loop
        ;;

    *)
        echo "mmcd — MMC Daemon (Termux Background Service)"
        echo ""
        echo "Usage: bash mmcd.sh <command>"
        echo ""
        echo "Commands:"
        echo "  start       🟢 Daemon စတင်မယ် (background)"
        echo "  stop        🛑 Daemon ရပ်မယ်"
        echo "  status      📊 အခြေအနေ ကြည့်မယ်"
        echo "  foreground  🔍 Debug mode (terminal မှာ)"
        ;;
esac
