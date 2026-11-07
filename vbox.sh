#!/bin/bash
# vbox.sh — CodeOS VirtualBox support.
#
# Boot the whole OS inside VirtualBox and verify its networking (including the
# genuine-OpenSSL https_get path).  Provisions the "CodeOS" VM if needed, wires
# NAT networking so the guest can reach host TLS-test services at 10.0.2.2 and
# the internet, and optionally captures serial console output for the
# https_boot_test verification.
#
# Usage:
#   ./vbox.sh                     # build ISO + disk, provision VM, boot GUI
#   ./vbox.sh --headless          # boot headless (VBoxHeadless)
#   ./vbox.sh --capture FILE      # boot + save serial console to FILE
#   ./vbox.sh --capture --headless FILE
#   ./vbox.sh --destroy           # unregister + delete the CodeOS VM
#   ./vbox.sh --status            # show VM config + guest-net details
set -euo pipefail

VM="CodeOS"
SRC="$(cd "$(dirname "$0")" && pwd)"
ISO="${SRC}/kernel/codeos-1.0.iso"
DISK="${SRC}/kernel/disk.img"
VDI="${SRC}/kernel/disk.vdi"
FULL_LOG="/tmp/codeos-vbox-serial.log"

# guest <-> host test endpoints (NAT): guest sees the host as 10.0.2.2.
# 8443 = self-signed TLS server used by https_boot_test stage1/2.
HOSTFWD=( "tcp:127.0.0.1:8443-:8443" )

info() { echo "==> $*"; }
die()  { echo "ERROR: $*" >&2; exit 1; }

# ── arch detection for raw disk: VirtualBox needs a converted VDI ----
need_vdi () {
    # The kernel Makefile creates the pre-baked image in kernel/, beside the ISO.
    if [ ! -f "${DISK}" ]; then
        info "building missing disk image"
        make -C "${SRC}/kernel" disk.img || die "failed to build ${DISK}"
    fi
    [ -f "${DISK}" ] || die "disk image build did not create ${DISK}"

    # Reconvert when the raw image changes so VirtualBox never boots a stale VDI.
    if [ ! -f "${VDI}" ] || [ "${DISK}" -nt "${VDI}" ]; then
        info "converting ${DISK} -> ${VDI}"
        rm -f "${VDI}"
        VBoxManage convertfromraw "${DISK}" "${VDI}" --format VDI
    fi
}

provision () {
    if VBoxManage showvminfo "${VM}" >/dev/null 2>&1; then
        info "VM '${VM}' already exists"
        return
    fi
    info "creating VM '${VM}'"
    VBoxManage createvm --name "${VM}" --ostype "Other" --register >/dev/null

    VBoxManage modifyvm "${VM}" \
        --memory 4096 --cpus 2 --vram 128 \
        --graphicscontroller vmsvga \
        --firmware bios \
        --boot1 dvd --boot2 disk \
        --nic1 nat --nictype1 82540EM \
        --cableconnected1 on \
        --audio none --usb on \
        --usbehci on \
        --vram 128

    # serial console -> file (used with --capture) and for guest->host verify
    VBoxManage modifyvm "${VM}" --uart1 0x3F8 4 --uartmode1 file "${FULL_LOG}"

    # storage: IDE controller only.  The kernel reads the rootfs via the legacy
    # PATA/IDE driver (drivers/ata.c @ ports 0x1F0/0x170); AHCI/SATA is not
    # compiled into the x86_64 kernel, so we must NOT use VirtualBox's SATA
    # controller.  VBox's IDE (PIIX3) exposes those legacy ports like QEMU -hda.
    VBoxManage storagectl "${VM}" --name "IDE" --add ide --controller PIIX3 >/dev/null 2>&1 || \
        VBoxManage storagectl "${VM}" --name "IDE" --add ide --controller PIIX3
    VBoxManage storagectl "${VM}" --name "SATA" --add sata --controller AHCI >/dev/null 2>&1 || true

    # NAT port-forward for host TLS test service (guest 10.0.2.2:8443)
    for pf in "${HOSTFWD[@]}"; do
        VBoxManage natpf "${VM}" "pf-${pf%%-*}" "${pf}" || true
    done
    info "VM '${VM}' provisioned (NAT NIC == e1000/82540EM)"
}

attach_media () {
    if [ ! -f "${ISO}" ]; then
        info "building missing ISO and disk image"
        make -C "${SRC}/kernel" codeos-1.0.iso || die "failed to build ${ISO}"
    fi
    [ -f "${ISO}" ] || die "ISO build did not create ${ISO}"
    need_vdi
    VBoxManage storagectl "${VM}" --name "IDE" --add ide --controller PIIX3 >/dev/null 2>&1 || true
    # rootfs MUST be IDE primary master (0:0) — kernel ata_init() probes the
    # master on 0x1F0 (device 0).  Boot CD on the secondary channel (1:0).
    VBoxManage storageattach "${VM}" --storagectl "IDE" --port 0 --device 0 \
        --type hdd --medium "${VDI}"
    VBoxManage storageattach "${VM}" --storagectl "IDE" --port 1 --device 0 \
        --type dvddrive --medium "${ISO}"
    info "attached rootfs disk (IDE 0:0 primary master) + ISO (IDE 1:0)"
}

cmd_boot () {
    need_vdi
    provision
    attach_media
    local headless="${1:-0}"
    if [ "${headless}" = "1" ]; then
        info "booting '${VM}' headless (VBoxHeadless)"
        VBoxManage startvm "${VM}" --type headless
    else
        info "booting '${VM}' GUI"
        VBoxManage startvm "${VM}" --type gui
    fi
    info "serial console: ${FULL_LOG}"
    cat <<EOF
  Network verification (in the guest):
    - 'https example.com 443 /'       -> genuine-OpenSSL TLS1.2/1.3 GET (internet)
    - boot with 'httpsboot' on cmdline -> runs https_boot_test (needs host:8443)
  To run the httpsboot stage test:
    1) start a self-signed TLS server on the HOST at 127.0.0.1:8443
       (guest reaches it via 10.0.2.2:8443)
    2) boot and at the Limine menu add  httpsboot  to the kernel cmdline
EOF
}

case "${1:-boot}" in
    boot)
        cmd_boot 0 ;;
    --headless)
        cmd_boot 1 ;;
    --capture)
        shift
        local f="${1:-/tmp/codeos-vbox-serial.log}"
        rm -f "${FULL_LOG}"
        cmd_boot 0
        echo "capturing serial to ${f} ... (Ctrl-C to stop)"
        until [ -s "${FULL_LOG}" ]; do sleep 1; done
        tail -f "${FULL_LOG}" | tee "$f" ;;
    --status)
        VBoxManage showvminfo "${VM}" 2>&1 | grep -iE "Name:|NIC 1|Memory|State|Attached|UART" || die "VM not provisioned" ;;
    --destroy)
        VBoxManage unregistervm "${VM}" --delete 2>&1 || die "no such VM" ;;
    *)
        echo "usage: $0 [boot|--headless|--capture [FILE]|--status|--destroy]"; exit 1 ;;
esac
