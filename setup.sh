echo "Installing dependencies..."
sudo pacman -Syu --needed --noconfirm base-devel git cmake nasm make clang qemu-full
git clone https://aur.archlinux.org/yay.git
cd yay
makepkg -si --noconfirm
yay -Syu --needed --noconfirm rustup
yay -Syu --needed --noconfirm rust-analyzer
yay -Syu --needed --noconfirm llvm
yay -Syu --needed --noconfirm lld
yay -Syu --needed --noconfirm libelf
sudo pacman -Syu --needed --noconfirm libelf
sudo pacman -Syu --needed --noconfirm libelf-devel
sudo pacman -Syu --needed --noconfirm qemu-full
sudo pacman -Syu --needed --noconfirm qemu-arch-extra
sudo pacman -Syu --needed --noconfirm qemu-guest-agent
sudo pacman -Syu --needed --noconfirm qemu-virtio
sudo pacman -Syu --needed --noconfirm qemu-virtio-win
yay -Syu --needed --noconfirm qemu-virtio-win-bin
rustup default stable
rustup update
echo "Dependencies installed successfully."
sleep 1
echo "Compiling CodeOS and dependencies..."
make -j$(nproc)
echo "CodeOS compiled successfully."
echo "Running CodeOS in QEMU..."
./run.sh