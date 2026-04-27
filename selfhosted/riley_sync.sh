#!/bin/bash
# =================================================================
#  riley_sync.sh  —  OneDrive Sync (No-Browser Method)
# =================================================================
#
#  Problem:
#      Termux on Android has no browser for rclone OAuth flow.
#      This script provides 3 alternative methods to authenticate
#      without needing a browser.
#
#  Method 1: Manual Token Copy (Recommended)
#      Run rclone authorize on a PC with browser, copy token to Termux.
#
#  Method 2: rclone Service Account (Advanced)
#      Use a pre-configured Service Account from Azure AD.
#
#  Method 3: REST API Direct (Lightweight)
#      Use curl + Microsoft Graph API with Client Credentials.
#
#  Usage:
#      bash riley_sync.sh setup     # Interactive first-time setup
#      bash riley_sync.sh sync      # Run one-way sync
#      bash riley_sync.sh loop      # Continuous sync (every 5 min)
#      bash riley_sync.sh status    # Show sync status
#
#  Target: Android Termux (ARMv8-A / aarch64)
#  Version: 1.0.0
# =================================================================

set -euo pipefail

# =================================================================
# Configuration
# =================================================================

RILEY_HOME="${RILEY_HOME:-$HOME/z}"
SYNC_DIR="$RILEY_HOME/sync"
RCLONE_CONF="$RILEY_HOME/.config/rclone/rclone.conf"
RCLONE_REMOTE="onedrive"
RCLONE_REMOTE_PATH="MMC_Backup"

# Sync settings
SYNC_INTERVAL="${SYNC_INTERVAL:-300}"  # 5 minutes (min: 300)

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

info()  { echo -e "${CYAN}[riley]${NC} $*"; }
ok()    { echo -e "${GREEN}[ok]${NC} $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC} $*" >&2; }
err()   { echo -e "${RED}[error]${NC} $*" >&2; }

# =================================================================
# Method 1: Manual Token Copy
# =================================================================
#
#  This is the SIMPLEST and MOST RELIABLE method:
#
#  Step 1: On a PC/Mac with a browser, run:
#          rclone authorize "onedrive"
#
#  Step 2: rclone will give you a long token string.
#          Copy it to clipboard.
#
#  Step 3: On Termux, paste the token when prompted.
#          It gets saved to rclone.conf automatically.
#
# =================================================================

setup_manual_token() {
    echo ""
    echo "================================================================"
    echo -e "  ${BOLD}Method 1: Manual Token Copy${NC}"
    echo ""
    echo "  This is the RECOMMENDED method for Termux."
    echo "  You need a PC/Mac with a browser for a one-time setup."
    echo ""
    echo "  ${BOLD}Step 1:${NC} On your PC, install rclone and run:"
    echo -e "          ${GREEN}rclone authorize \"onedrive\"${NC}"
    echo ""
    echo "  ${BOLD}Step 2:${NC} A browser window will open. Sign in to Microsoft."
    echo "          After authorization, rclone will show a token like:"
    echo "          {\"access_token\":\"eyJ0...\",\"token_type\":\"Bearer\",...}"
    echo ""
    echo "  ${BOLD}Step 3:${NC} Copy that ENTIRE token string (including braces)."
    echo ""
    echo "  ${BOLD}Step 4:${NC} Come back to Termux and paste it below."
    echo "================================================================"
    echo ""

    # Ensure rclone config directory exists
    mkdir -p "$(dirname "$RCLONE_CONF")"

    # Check if rclone is installed
    if ! command -v rclone &>/dev/null; then
        err "rclone is not installed."
        echo ""
        echo "  Install on Termux:"
        echo -e "    ${GREEN}pkg install rclone${NC}"
        echo ""
        echo "  Or download directly:"
        echo -e "    ${GREEN}curl https://rclone.org/install.sh | bash${NC}"
        return 1
    fi

    # Use rclone config with noninteractive token paste
    echo -e "${BOLD}Paste your rclone token now (press Enter when done):${NC}"
    echo -e "${YELLOW}(Paste the full JSON token from your PC)${NC}"
    echo ""

    # Create/update rclone config
    if [[ -f "$RCLONE_CONF" ]] && grep -q "\[$RCLONE_REMOTE\]" "$RCLONE_CONF"; then
        warn "Remote '$RCLONE_REMOTE' already exists in rclone.conf"
        echo -e "  To reconfigure: ${GREEN}rclone config reconnect $RCLONE_REMOTE${NC}"
        echo -e "  Then paste the token when prompted."
        echo ""
        rclone config reconnect "$RCLONE_REMOTE" --auto-confirm
    else
        rclone config create "$RCLONE_REMOTE" onedrive \
            --auto-confirm 2>&1
    fi

    if [[ $? -eq 0 ]]; then
        ok "OneDrive remote configured successfully!"
        echo ""
        echo "  Test connection:"
        echo -e "    ${GREEN}rclone lsd $RCLONE_REMOTE:${NC}"
    else
        err "Configuration failed."
        echo "  Make sure you pasted the full token."
    fi
}

# =================================================================
# Method 2: Service Account / App Registration
# =================================================================
#
#  For users who have Azure AD access or can register an app:
#
#  1. Go to https://portal.azure.com/ → App registrations → New
#  2. Set redirect URI to: https://login.microsoftonline.com/common/oauth2/nativeclient
#  3. Note the Client ID and Client Secret
#  4. Configure rclone with these credentials
#
# =================================================================

setup_service_account() {
    echo ""
    echo "================================================================"
    echo -e "  ${BOLD}Method 2: Azure AD App Registration${NC}"
    echo ""
    echo "  Use this if you have access to Azure AD or Microsoft 365 admin."
    echo ""
    echo "  ${BOLD}Setup Steps:${NC}"
    echo ""
    echo "  1. Go to: https://portal.azure.com/#blade/Microsoft_AAD_RegisteredApps"
    echo "  2. Click 'New registration'"
    echo "  3. Name: 'MMC Riley Sync'"
    echo "  4. Redirect URI type: 'Mobile and desktop applications'"
    echo "  5. Redirect URI: https://login.microsoftonline.com/common/oauth2/nativeclient"
    echo "  6. Click 'Register'"
    echo "  7. Copy 'Application (client) ID' → this is your Client ID"
    echo "  8. Go to 'Certificates & secrets' → 'New client secret'"
    echo "  9. Copy the secret value"
    echo ""
    echo "  ${BOLD}Configure rclone:${NC}"
    echo ""
    echo -e "    ${GREEN}rclone config${NC}"
    echo "    → n (new remote)"
    echo "    → Name: $RCLONE_REMOTE"
    echo "    → Type: onedrive"
    echo "    → Client ID: [paste your Client ID]"
    echo "    → Client Secret: [paste your Client Secret]"
    echo "    → Region: global"
    echo "    → Auto config: n (NO! We skip browser OAuth)"
    echo "    → [rclone will provide a link to visit on PC]"
    echo "    → Visit the link on your PC, authorize, copy the code back"
    echo "================================================================"
    echo ""

    mkdir -p "$(dirname "$RCLONE_CONF")"

    if ! command -v rclone &>/dev/null; then
        err "rclone is not installed. Run: pkg install rclone"
        return 1
    fi

    rclone config
}

# =================================================================
# Method 3: Graph API Direct (curl-based)
# =================================================================
#
#  For maximum control without rclone dependency.
#  Uses Microsoft Graph API directly with Client Credentials.
#
# =================================================================

setup_graph_api() {
    echo ""
    echo "================================================================"
    echo -e "  ${BOLD}Method 3: Microsoft Graph API Direct (curl)${NC}"
    echo ""
    echo "  Lightest option — no rclone needed, just curl."
    echo "  Requires Azure AD App Registration (see Method 2 steps 1-9)."
    echo ""
    echo "  ${BOLD}How it works:${NC}"
    echo "    1. Register app in Azure AD (see Method 2)"
    echo "    2. Get Client ID + Client Secret"
    echo "    3. Use Client Credentials flow (no user interaction needed)"
    echo "    4. Upload files via Graph API /drive/items/root:/{path}/content"
    echo ""
    echo "  ${BOLD}Limitations:${NC}"
    echo "    - Client Credentials flow accesses a SERVICE account's OneDrive,"
    echo "      not a personal OneDrive"
    echo "    - For personal OneDrive, use Method 1 or 2 with delegated auth"
    echo "================================================================"
    echo ""

    # Create Graph API sync script
    local graph_script="$RILEY_HOME/bin/riley_graph_sync.sh"
    mkdir -p "$RILEY_HOME/bin"

    cat > "$graph_script" << 'GRAPH_SCRIPT'
#!/bin/bash
# Riley Graph API Sync — Microsoft Graph Direct
# Requires: CLIENT_ID and CLIENT_SECRET environment variables

set -euo pipefail

GRAPH_BASE="https://graph.microsoft.com/v1.0"
TOKEN_URL="https://login.microsoftonline.com/common/oauth2/v2.0/token"
SCOPE="https://graph.microsoft.com/.default"

# Read config
CONFIG_DIR="$HOME/z/.config/riley"
source "$CONFIG_DIR/graph.conf" 2>/dev/null || {
    echo "ERROR: $CONFIG_DIR/graph.conf not found"
    echo "Create it with: CLIENT_ID=xxx  CLIENT_SECRET=xxx"
    exit 1
}

# Get access token
token_resp=$(curl -s -X POST "$TOKEN_URL" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "client_id=$CLIENT_ID" \
    -d "client_secret=$CLIENT_SECRET" \
    -d "scope=$SCOPE" \
    -d "grant_type=client_credentials")

ACCESS_TOKEN=$(echo "$token_resp" | grep -o '"access_token":"[^"]*"' | cut -d'"' -f4)

if [[ -z "$ACCESS_TOKEN" ]]; then
    echo "ERROR: Failed to get access token"
    echo "$token_resp"
    exit 1
fi

# Upload a file
upload_file() {
    local local_path="$1"
    local remote_path="$2"

    curl -s -X PUT \
        "$GRAPH_BASE/me/drive/root:/$remote_path:/content" \
        -H "Authorization: Bearer $ACCESS_TOKEN" \
        -H "Content-Type: application/octet-stream" \
        --data-binary @"$local_path"
}

# List files
list_files() {
    curl -s "$GRAPH_BASE/me/drive/root/children" \
        -H "Authorization: Bearer $ACCESS_TOKEN" | python3 -m json.tool
}

case "${1:-}" in
    upload)
        upload_file "$2" "$3"
        ;;
    list)
        list_files
        ;;
    *)
        echo "Usage: $0 upload <local> <remote> | list"
        ;;
esac
GRAPH_SCRIPT

    warn "Graph API script created: $graph_script"
    warn "Requires Azure AD App Registration with Client Credentials."
    warn "Recommended: Use Method 1 (Manual Token Copy) instead."
}

# =================================================================
# Sync Operations
# =================================================================

do_sync() {
    local ts
    ts=$(date '+%Y-%m-%d %H:%M:%S')

    if ! command -v rclone &>/dev/null; then
        err "rclone not installed. Run: pkg install rclone"
        return 1
    fi

    info "[$ts] Starting OneDrive sync..."

    # Sync local -> remote (one-way upload)
    rclone sync "$SYNC_DIR" "$RCLONE_REMOTE:$RCLONE_REMOTE_PATH" \
        --progress \
        --verbose \
        --exclude '.git/**' \
        --exclude '__pycache__/**' \
        --exclude '*.pyc' \
        --exclude 'node_modules/**' \
        --exclude '*.o' \
        --exclude '*.tmp' \
        --backup-dir "$RCLONE_REMOTE:$RCLONE_REMOTE_PATH/_archive" \
        --suffix "$(date +%Y%m%d_%H%M%S)" \
        --log-file "$RILEY_HOME/logs/riley_sync.log" \
        --log-level INFO 2>&1

    local rc=$?
    if [[ $rc -eq 0 ]]; then
        ok "[$ts] Sync completed successfully."
    else
        err "[$ts] Sync failed (exit code: $rc)"
    fi
}

sync_loop() {
    info "Continuous sync mode (interval: ${SYNC_INTERVAL}s)"
    info "Press Ctrl+C to stop."

    trap 'echo ""; info "Stopping continuous sync."; exit 0' SIGINT SIGTERM

    mkdir -p "$RILEY_HOME/logs"

    while true; do
        do_sync
        info "Next sync in ${SYNC_INTERVAL}s..."
        sleep "$SYNC_INTERVAL"
    done
}

show_status() {
    echo ""
    echo "Riley Sync Status"
    echo "================="

    if ! command -v rclone &>/dev/null; then
        err "rclone not installed"
        return 1
    fi

    # Check if remote is configured
    if [[ -f "$RCLONE_CONF" ]] && grep -q "\[$RCLONE_REMOTE\]" "$RCLONE_CONF"; then
        ok "Remote '$RCLONE_REMOTE' configured"
    else
        err "Remote '$RCLONE_REMOTE' not configured"
        echo "  Run: bash $0 setup"
        return 1
    fi

    # Test connection
    info "Testing connection to OneDrive..."
    rclone lsd "$RCLONE_REMOTE:" 2>&1 | head -5
    echo ""

    # Show sync directory
    info "Sync directory: $SYNC_DIR"
    info "Remote path    : $RCLONE_REMOTE:$RCLONE_REMOTE_PATH"

    if [[ -d "$SYNC_DIR" ]]; then
        local file_count
        file_count=$(find "$SYNC_DIR" -type f 2>/dev/null | wc -l)
        info "Local files    : $file_count"
    fi

    echo ""
}

# =================================================================
# Main
# =================================================================

main() {
    case "${1:-help}" in
        setup)
            echo ""
            echo "Choose an authentication method:"
            echo ""
            echo "  1) Manual Token Copy (Recommended — easiest)"
            echo "  2) Azure AD App Registration (Advanced)"
            echo "  3) Graph API Direct (Experimental)"
            echo ""
            echo -n "  Enter choice [1-3]: "
            read -r choice

            case "$choice" in
                1) setup_manual_token ;;
                2) setup_service_account ;;
                3) setup_graph_api ;;
                *) err "Invalid choice" ;;
            esac
            ;;
        sync)
            do_sync
            ;;
        loop)
            sync_loop
            ;;
        status)
            show_status
            ;;
        help|--help|-h|"")
            echo "riley_sync.sh — OneDrive Sync (No-Browser)"
            echo ""
            echo "Usage: bash riley_sync.sh <command>"
            echo ""
            echo "Commands:"
            echo "  setup     Interactive first-time authentication setup"
            echo "  sync      Run one-way sync (local -> OneDrive)"
            echo "  loop      Continuous sync (every ${SYNC_INTERVAL}s)"
            echo "  status    Show sync status and connection test"
            echo ""
            echo "Recommended method:"
            echo "  1. Install rclone: pkg install rclone"
            echo "  2. Run: bash riley_sync.sh setup"
            echo "  3. Choose Method 1 (Manual Token Copy)"
            echo "  4. On your PC: rclone authorize \"onedrive\""
            echo "  5. Copy the token back to Termux"
            echo "  6. Run: bash riley_sync.sh sync"
            ;;
        *)
            err "Unknown command: $1"
            echo "Run 'bash riley_sync.sh help' for usage."
            exit 1
            ;;
    esac
}

main "$@"
