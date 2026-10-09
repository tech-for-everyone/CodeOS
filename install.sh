#!/bin/bash
echo "Installing dependencies..."
sudo pacman -Syu --noconfirm rust 
sudo apt update && sudo apt install -y build-essential libssl-dev pkg-config
sudo dnf install -y rust cargo openssl-devel pkgconfig
echo "Dependencies installed successfully."
echo "COMPILING PLEASE WAIT..."
cargo build --release
sleep 2
if [ $? -eq 0 ]; then
    echo "Installation completed successfully."
else
    echo "Installation failed."
fi
echo "MOVING PROGRAM TO /usr/bin/"
sudo cp target/release/hyperde /usr/bin/hyperde