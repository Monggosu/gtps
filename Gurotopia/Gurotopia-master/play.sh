#!/bin/bash
# One command to get Gurotopia running: installs what is missing, builds,
# sets up MariaDB, seeds the test accounts, starts the server, and prints
# the two lines you need to add as hosts on your phone.
#
# Usage:  ./play.sh
#
set -e
cd "$(dirname "$0")"

say()  { echo -e "\n== $1"; }
fail() { echo -e "\nERROR: $1"; exit 1; }

[ "$(uname)" = "Linux" ] || fail "This script is for Linux. See the README for Windows/macOS."
command -v apt >/dev/null 2>&1 || fail "This script needs 'apt' (Debian/Ubuntu). On another distro, install the packages from the README manually, then run this script again."

# ---------- 1. dependencies ----------
NEED=""
dpkg -s build-essential  >/dev/null 2>&1 || NEED="$NEED build-essential"
dpkg -s libssl-dev       >/dev/null 2>&1 || NEED="$NEED libssl-dev"
dpkg -s libmariadb-dev   >/dev/null 2>&1 || NEED="$NEED libmariadb-dev"
dpkg -s mariadb-server   >/dev/null 2>&1 || NEED="$NEED mariadb-server"
dpkg -s unzip            >/dev/null 2>&1 || NEED="$NEED unzip"

if [ -n "$NEED" ]; then
    say "Installing:$NEED (this needs your password, and needs internet once)"
    sudo apt update
    sudo apt install -y $NEED
else
    say "All required packages are already installed."
fi

# g++ new enough for C++20 std::format? if not, try g++-13
CXX=g++
if ! echo '#include <format>
int main(){ (void)std::format("{}",1); }' | g++ -std=c++20 -x c++ - -o /tmp/_fmtcheck 2>/dev/null; then
    say "Default g++ is too old for this project. Installing g++-13..."
    sudo apt install -y g++-13
    CXX=g++-13
fi
rm -f /tmp/_fmtcheck

# ---------- 2. MariaDB running ----------
say "Starting MariaDB..."
sudo systemctl enable --now mariadb >/dev/null 2>&1 || sudo service mariadb start

# ---------- 3. database user (idempotent: safe to run again) ----------
DB_PASS_FILE=".gurotopia_db_password"
if [ ! -f "$DB_PASS_FILE" ]; then
    DB_PASS=$(tr -dc 'A-Za-z0-9' </dev/urandom | head -c 24)
    echo "$DB_PASS" > "$DB_PASS_FILE"
    chmod 600 "$DB_PASS_FILE"
else
    DB_PASS=$(cat "$DB_PASS_FILE")
fi

say "Setting up the 'guro' database user..."
sudo mariadb <<SQL
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

# ---------- 5. start the server once, briefly, so it creates its tables ----------
say "Starting the server for the first time to set up the database..."
./main.out & SERVER_PID=$!
sleep 6
if ! kill -0 $SERVER_PID 2>/dev/null; then
    fail "The server stopped immediately. Scroll up for the error it printed."
fi

# ---------- 6. seed the test accounts (safe to run again) ----------
if [ -f tools-admin/seed_test_accounts.sql ]; then
    say "Creating the administrator / bot1 / bot2 / bot3 accounts..."
    mysql -u guro -p"$DB_PASS" gurotopia < tools-admin/seed_test_accounts.sql
fi

say "Stopping the setup run, starting the real server (auto-restarts if it crashes)..."
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

chmod +x run_server.sh
nohup ./run_server.sh > server.log 2>&1 &
disown
sleep 2

# ---------- 7. tell the person what to do on their phone ----------
IP=$(hostname -I | awk '{print $1}')
[ -z "$IP" ] && IP="(couldn't detect automatically - run: hostname -I)"

echo
echo "================================================================"
echo " Server is running in the background. Log: $(pwd)/server.log"
echo
echo " Accounts ready to log in with:"
echo "   administrator / administrator   (role: administrator)"
echo "   bot1 / bot1, bot2 / bot2, bot3 / bot3   (role: player)"
echo
echo " On your PHONE, make growtopia1.com and growtopia2.com point to"
echo " this computer. Your phone must be on the SAME WiFi as this PC."
echo " This computer's address right now is:"
echo
echo "     $IP"
echo
echo " Add these two lines wherever your hosts app / hosts file asks:"
echo "     $IP growtopia1.com"
echo "     $IP growtopia2.com"
echo
echo " To stop the server later:  pkill -f run_server.sh; pkill main.out"
echo " To start it again later:   ./run_server.sh &"
echo "================================================================"
