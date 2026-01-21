#!/bin/bash
# make_installer.sh - Generate self-extracting installer
set -e

VERSION="1.0.2"
INSTALLER_NAME="qmem_install.sh"
PAYLOAD_DIR="installer_payload"

# Check build
if [ ! -f "bin/qmemd" ] || [ ! -f "bin/qmemctl" ]; then
    echo "Error: Binaries not found. Run 'make all plugins' first."
    exit 1
fi

# Prepare payload directory
rm -rf "$PAYLOAD_DIR"
mkdir -p "$PAYLOAD_DIR"/bin
mkdir -p "$PAYLOAD_DIR"/plugins
mkdir -p "$PAYLOAD_DIR"/config
mkdir -p "$PAYLOAD_DIR"/systemd

# Copy files
cp bin/qmemd "$PAYLOAD_DIR"/bin/
cp bin/qmemctl "$PAYLOAD_DIR"/bin/
cp plugins/*.so "$PAYLOAD_DIR"/plugins/
cp config/qmem.conf.example "$PAYLOAD_DIR"/config/qmem.conf
cp debian/qmem.service "$PAYLOAD_DIR"/systemd/

# Create version file
echo "$VERSION" > "$PAYLOAD_DIR"/version

# Create tarball
tar -czf payload.tar.gz -C "$PAYLOAD_DIR" .

# Generate installer script
cat > "$INSTALLER_NAME" << 'EOF'
#!/bin/bash
# QMem Self-Extracting Installer
set -e

# Check root
if [ "$(id -u)" != "0" ]; then
    echo "This script must be run as root" 1>&2
    exit 1
fi

ARCHIVE=$(awk '/^__ARCHIVE_BELOW__/ {print NR + 1; exit 0; }' "$0")

echo "=== QMem Installer ==="
echo "Extracting..."

mkdir -p /tmp/qmem_install
tail -n+$ARCHIVE "$0" | tar xz -C /tmp/qmem_install

cd /tmp/qmem_install

echo "Installing binaries to /usr/local/bin..."
install -m 755 bin/qmemd /usr/local/bin/
install -m 755 bin/qmemctl /usr/local/bin/

echo "Installing plugins to /usr/lib/qmem/plugins..."
mkdir -p /usr/lib/qmem/plugins
install -m 755 plugins/*.so /usr/lib/qmem/plugins/

echo "Installing configuration to /etc/qmem..."
if [ ! -f /etc/qmem/qmem.conf ]; then
    mkdir -p /etc/qmem
    install -m 644 config/qmem.conf /etc/qmem/
else
    echo "Config exists at /etc/qmem/qmem.conf, skipping overwrite."
fi

# Install systemd service if available
if command -v systemctl >/dev/null 2>&1; then
    echo "Installing systemd service..."
    
    # Check if we need to modify path in service file
    # Default service file points to /usr/bin/qmemd, we installed to /usr/local/bin/qmemd
    sed 's|/usr/bin/qmemd|/usr/local/bin/qmemd|g' systemd/qmem.service > /lib/systemd/system/qmem.service
    chmod 644 /lib/systemd/system/qmem.service
    
    echo "Reloading systemd..."
    systemctl daemon-reload
    
    echo "Starting qmem service..."
    systemctl enable qmem
    systemctl restart qmem
    
    echo "Done! Service is running."
else
    echo "Systemd not found. You can start the daemon manually:"
    echo "  sudo qmemd -d"
fi

# Cleanup
rm -rf /tmp/qmem_install

echo "Installation complete!"
echo "Run 'sudo qmemctl status' to check status."

exit 0

__ARCHIVE_BELOW__
EOF

# Append payload
cat payload.tar.gz >> "$INSTALLER_NAME"
chmod +x "$INSTALLER_NAME"

echo "Created $INSTALLER_NAME"

# Cleanup
rm -rf "$PAYLOAD_DIR" payload.tar.gz
