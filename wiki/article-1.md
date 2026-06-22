The first article on CodeOS in this article you will learn  how to run CodeOS inside qemu
commands will only be given to arch users because me the creator daily drives arch 

## Installing tools
Arch users first step to running CodeOS is to get QEMU with this command
### sudo pacman -Syu qemu-full git
Next we need to grant qemu permisions to start VMs so for that please go on your BIOS if you have an AMD CPU search for the SVM or for newer CPUs SEV for intel it is Intel Virtualization Tech
## Seting up VMs
Rigth now you have rebooted onto your desktop (if you have one ) next we need to tell qemu to enable VMs so type this command into the terminal
### sudo systemctl enable libvirtd
### sudo systemctl start libvirtd
These commands are crucial to the process 
## Running the VM
To run the vm you need to download the kernel so go to your browser and search for
# https://github.com/tech-for-everyone/CodeOS-Kernel/releases/tag/Releases
and download the codeos-1.2-kernel.bin and Makefile
once that is done go to the dir you downloaded it to and run 
### make run