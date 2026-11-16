#!/bin/bash
# One command to get Gurotopia running on a fresh Ubuntu/Debian machine
# (laptop or VPS). It installs what is missing, sets up MariaDB, builds,
# creates the test accounts, writes the server address into server_data.php,
# opens the firewall ports, starts the server and prints the hosts lines
# you need on your phone.
#
# Usage:   bash play.sh
# Force the address yourself (e.g. VPS behind NAT):
#          SERVER_IP=1.2.3.4 bash play.sh
set -e
cd "$(dirname "$0")"

say()  { echo -e "\n== $1"; }
fail() { echo -e "\nERROR: $1"; exit 1; }

[ "$(uname)" = "Linux" ] || fail "This script is for Linux. See the README for Windows/macOS."
command -v apt-get >/dev/null 2>&1 || fail "This script needs apt (Debian/Ubuntu)."

# The server listens on port 443, which needs root. Re-run as root if needed.
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 || fail "Run this as root (su -), or install sudo."
    exec sudo -E bash "$0" "$@"
fi

[ -f items.dat ] || fail "items.dat is missing in $(pwd). Copy it into this folder, then run again."

# ---------- 1. dependencies ----------
NEED=""
for p in build-essential libssl-dev libmariadb-dev mariadb-server mariadb-client unzip curl; do
    dpkg -s "$p" >/dev/null 2>&1 || NEED="$NEED $p"
done
export DEBIAN_FRONTEND=noninteractive
if [ -n "$NEED" ]; then
    say "Installing:$NEED (needs internet, takes a minute)"
    apt-get update
    apt-get install -y $NEED
else
    say "All required packages are already installed."
fi

# g++ new enough for C++20 std::format? if not, install g++-13
CXX=g++
if ! echo '#include <format>
int main(){ (void)std::format("{}",1); }' | g++ -std=c++20 -x c++ - -o /tmp/_fmtcheck 2>/dev/null; then
    say "Default g++ is too old for this project. Installing g++-13..."
    apt-get install -y g++-13
    CXX=g++-13
fi
rm -f /tmp/_fmtcheck

# ---------- 2. MariaDB running ----------
say "Starting MariaDB..."
systemctl enable --now mariadb >/dev/null 2>&1 || service mariadb start || true
for i in $(seq 1 30); do
    mariadb -e "select 1" >/dev/null 2>&1 && break
    sleep 1
done
mariadb -e "select 1" >/dev/null 2>&1 || fail "MariaDB did not start. Try: service mariadb start"

# ---------- 3. database user (safe to run again) ----------
DB_PASS_FILE=".gurotopia_db_password"
if [ ! -f "$DB_PASS_FILE" ]; then
    DB_PASS=$(tr -dc 'A-Za-z0-9' </dev/urandom | head -c 24)
    echo "$DB_PASS" > "$DB_PASS_FILE"
    chmod 600 "$DB_PASS_FILE"
else
    DB_PASS=$(cat "$DB_PASS_FILE")
fi

say "Setting up the 'guro' database user..."
mariadb <<SQL
CREATE USER IF NOT EXISTS 'guro'@'localhost' IDENTIFIED BY '$DB_PASS';
ALTER USER 'guro'@'localhost' IDENTIFIED BY '$DB_PASS';
CREATE DATABASE IF NOT EXISTS gurotopia;
GRANT ALL PRIVILEGES ON gurotopia.* TO 'guro'@'localhost';
FLUSH PRIVILEGES;
SQL

cat > mysql_login.txt <<CFG
host|127.0.0.1
user|guro
password|$DB_PASS
CFG

# ---------- 4. build ----------
say "Building (this can take a few minutes the first time)..."
make -j"$(nproc)" CXX=$CXX || fail "Build failed. Copy the error above and send it back."
[ -f main.out ] || fail "Build finished but main.out was not created."

# ---------- 5. address of this machine ----------
is_private() {
    case "$1" in
        10.*|192.168.*|172.1[6-9].*|172.2[0-9].*|172.3[0-1].*|127.*|169.254.*) return 0 ;;
    esac
    return 1
}
LAN_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
PUB_IP=$(curl -4 -s --max-time 5 https://api.ipify.org 2>/dev/null || true)

IP_NOTE=""
if [ -n "$SERVER_IP" ]; then
    IP="$SERVER_IP"
elif [ -n "$LAN_IP" ] && ! is_private "$LAN_IP"; then
    IP="$LAN_IP"                       # VPS with a public address on the network card
elif [ -n "$LAN_IP" ]; then
    IP="$LAN_IP"                       # home network (laptop / PC)
    if [ -n "$PUB_IP" ] && [ "$PUB_IP" != "$LAN_IP" ]; then
        IP_NOTE="Using the local network address. If this is a VPS (not your own laptop), run:  SERVER_IP=$PUB_IP bash play.sh"
    fi
elif [ -n "$PUB_IP" ]; then
    IP="$PUB_IP"
else
    fail "Could not detect this machine's address. Run:  SERVER_IP=<address> bash play.sh"
fi
if is_private "$IP"; then PUBLIC=no; else PUBLIC=yes; fi

# ---------- 6. first start (creates tables and server_data.php) ----------
pkill -f run_server.sh 2>/dev/null || true
pkill -x main.out 2>/dev/null || true
sleep 1

say "Starting the server once to create the database tables..."
./main.out > /tmp/gurotopia_setup.log 2>&1 & SERVER_PID=$!
sleep 6
if ! kill -0 $SERVER_PID 2>/dev/null; then
    cat /tmp/gurotopia_setup.log
    fail "The server stopped immediately. The error it printed is above."
fi

# ---------- 7. test accounts ----------
if [ -f tools-admin/seed_test_accounts.sql ]; then
    say "Creating the administrator / bot1 / bot2 / bot3 accounts..."
    mariadb -u guro -p"$DB_PASS" gurotopia < tools-admin/seed_test_accounts.sql >/dev/null
    ACC_PASS=""
    if [ "$PUBLIC" = "yes" ]; then
        # exposed to the internet: do not keep the default passwords
        ACC_FILE=".gurotopia_account_password"
        if [ ! -f "$ACC_FILE" ]; then
            tr -dc 'A-Za-z0-9' </dev/urandom | head -c 12 > "$ACC_FILE"
            chmod 600 "$ACC_FILE"
        fi
        ACC_PASS=$(cat "$ACC_FILE")
        mariadb -u guro -p"$DB_PASS" gurotopia -e \
            "UPDATE peer SET password='$ACC_PASS' WHERE growid IN ('administrator','bot1','bot2','bot3');"
    fi
fi

# ---------- 8. server address + firewall ----------
if [ -f server_data.php ]; then
    sed -i "s/^server|.*/server|$IP/" server_data.php
else
    fail "server_data.php was not created by the server."
fi

if command -v ufw >/dev/null 2>&1 && ufw status 2>/dev/null | grep -q "Status: active"; then
    say "Opening ports 443/tcp and 17091/udp in ufw..."
    ufw allow 443/tcp >/dev/null
    ufw allow 17091/udp >/dev/null
fi

# ---------- 9. start the real server ----------
say "Stopping the setup run, starting the real server (auto-restarts if it crashes)..."
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
chmod +x run_server.sh
nohup ./run_server.sh > server.log 2>&1 &
disown
sleep 4
pgrep -x main.out >/dev/null 2>&1 || { tail -20 server.log; fail "The server is not running. The log is above."; }

# ---------- 10. what to do on the phone ----------
cat > hosts-hp.txt <<HOSTS
127.0.0.1       localhost
::1             ip6-localhost

$IP growtopia1.com
$IP growtopia2.com
$IP www.growtopia1.com
$IP www.growtopia2.com
HOSTS

echo
echo "================================================================"
echo " Server is running in the background."
echo " Log: $(pwd)/server.log"
echo
echo " Accounts you can log in with:"
if [ -n "$ACC_PASS" ]; then
echo "   administrator / $ACC_PASS   (role: administrator)"
echo "   bot1, bot2, bot3 / $ACC_PASS   (role: player)"
else
echo "   administrator / administrator   (role: administrator)"
echo "   bot1 / bot1, bot2 / bot2, bot3 / bot3   (role: player)"
fi
echo
echo " Server address: $IP"
[ -n "$IP_NOTE" ] && echo " NOTE: $IP_NOTE"
[ "$PUBLIC" = "no" ] && echo " (Your phone must be on the SAME WiFi as this computer.)"
[ "$PUBLIC" = "yes" ] && echo " (If it does not connect, also open 443/tcp and 17091/udp in your provider's firewall panel.)"
echo
echo " On your PHONE (Virtual Hosts app), use these 4 lines"
echo " (also saved in: $(pwd)/hosts-hp.txt):"
echo "     $IP growtopia1.com"
echo "     $IP growtopia2.com"
echo "     $IP www.growtopia1.com"
echo "     $IP www.growtopia2.com"
echo
echo " Stop the server later:   pkill -f run_server.sh; pkill main.out"
echo " Start it again later:    bash play.sh     (safe to run again)"
echo "================================================================"
