# Launching the refdesign-sim demo

Table of Contents
=================

   * [Launching the refdesign-sim demo](#launching-the-refdesign-sim-demo)
      * [Overview](#overview)
      * [Preparation step](#preparation-step)
      * [Clone and build Xilinx QEMU on the host](#clone-and-build-xilinx-qemu-on-the-host)
      * [Create a disk image for Ubuntu LTS](#create-a-disk-image-for-ubuntu-lts)
      * [Install Ubuntu LTS on the disk image](#install-ubuntu-lts-on-the-disk-image)
      * [Required packages in the Ubuntu LTS 20.04 VM](#required-packages-in-the-ubuntu-lts-2004-vm)
      * [Enable the iommu in the Ubuntu LTS 20.04 VM](#enable-the-iommu-in-the-ubuntu-lts-2004-vm)
      * [Install SystemC in the Ubuntu LTS 20.04 VM](#install-systemc-in-the-ubuntu-lts-2004-vm)
      * [Install Verilator in the Ubuntu LTS 20.04 VM](#install-verilator-in-the-ubuntu-lts-2004-vm)
      * [Build libsystemctlm-soc PCIe RTL VFIO demos in the Ubuntu LTS 20.04 VM](#build-libsystemctlm-soc-pcie-rtl-vfio-demos-in-the-ubuntu-lts-2004-vm)
      * [Launching QEMU and the refdesign-sim demo](#launching-qemu-and-the-refdesign-sim-demo)
      * [Exercising the refdesign-sim demo with VFIO test applications](#exercising-the-refdesign-sim-demo-with-vfio-test-applications)

## Overview 

Instructions for how to build and run an Ubuntu based guest system with Xilinx
QEMU together with the PCIe EP refdesign-sim demo are found below. More
information about the PCIe EP HW bridges are found in
[../../../docs/pcie-rtl-bridges/overview.md](../../../docs/pcie-rtl-bridges/overview.md).

## Preparation step

1. Download the Ubuntu LTS 20.04 installer from www.ubuntu.com

## Clone and build Xilinx QEMU on the host

Clone and configure qemu by issuing the following commands:

```
~$ git clone https://github.com/Xilinx/qemu.git
~$ mkdir qemu-build
~$ cd qemu-build
~/qemu-build$ ../qemu/configure \
    --target-list=microblazeel-softmmu,aarch64-softmmu,x86_64-softmmu \
    --enable-sdl --enable-sdl-image
```

Make sure to that SDL support is enabled on the output printed by
configure.

```
...
SDL support       yes (2.0.9)
SDL image support yes
...
```

In case SDL support is missing install the required development packages
on the host and rerun configure (below example is for Debian / Ubuntu
systems):

```
~$ sudo apt-get install libsdl2-dev libsdl2-image-dev
```

Finally build QEMU:
```
~/qemu-build$ make
```

The following two binaries will be found after the build:

```
~/qemu-build$ ls qemu-img
~/qemu-build$ ls x86_64-softmmu//qemu-system-x86_64
```

## Create a disk image for Ubuntu LTS

Create a disk image where Ubuntu LTS 20.04 will be installed using the
following command:

```
~/qemu-build/qemu-img create -f qcow2 hd0.qcow2 10G
```

## Install Ubuntu LTS on the disk image

Launch the Ubuntu LTS 20.04 installer (ubuntu-20.04-desktop-amd64.iso) in
QEMU with the newly created disk image attached using the following
command (please change 'accel=kvm' to 'accel=tcg' in case the user is not
allowed to use kvm):

```
$ ~/qemu-build/x86_64-softmmu/qemu-system-x86_64                           \
    -M q35,accel=kvm,kernel-irqchip=split -m 4G -smp 4 -enable-kvm         \
    -drive file=hd0.qcow2,format=qcow2 -display sdl                        \
    -device virtio-net-pci,netdev=net0 -netdev type=user,id=net0           \
    -serial mon:stdio -machine-path /tmp/qemu                              \
    -device intel-iommu,intremap=on,device-iotlb=on                        \
    -device ioh3420,id=rootport,slot=0 -device ioh3420,id=rootport1,slot=1 \
    -cdrom ubuntu-20.04-desktop-amd64.iso
```

Above command provides network access through a virtio-net-pci device.

Proceed with installing Ubuntu LTS 20.04 and restart QEMU without the
*-cdrom ubuntu-20.04-desktop-amd64.iso* arguments when done.

## Required packages in the Ubuntu LTS 20.04 VM

Login into the Ubuntu LTS 20.04 VM and install below packages:
```
sudo apt-get install git build-essential autoconf flex bison
```

## Enable the iommu in the Ubuntu LTS 20.04 VM

Enable the iommu by adding *intel_iommu=on* to the Linux kernel's
command line and rebooting the system.

```
$ sudo sed -i 's/\(GRUB_CMDLINE_LINUX_DEFAULT.*\)"/\1 intel_iommu=on"/' /etc/default/grub
$ sudo update-grub
$ sudo reboot
```

Verify that the command line contains *intel_iommu=on* after rebooting by
issuing:
```
$ cat /proc/cmdline
BOOT_IMAGE=/boot/vmlinuz-5.8.0-36-generic root=UUID=0b68e1e4-5cf8-4db2-a476-3eea6e3d46c2 ro quiet splash intel_iommu=on vt.handoff=7
```


## Install SystemC in the Ubuntu LTS 20.04 VM

Download and install SystemC 2.3.2 by issuing the following commands in
the VM:

```
$ cd ~/Downloads/
$ wget -O systemc-2.3.2.tar.gz http://www.accellera.org/images/downloads/standards/systemc/systemc-2.3.2.tar.gz
$ tar xzf systemc-2.3.2.tar.gz
$ cd systemc-2.3.2
$ ./configure --prefix=/opt/systemc-2.3.2
$ make
$ sudo make install
```

## Install Verilator in the Ubuntu LTS 20.04 VM

Download and install Verilator v4.010 by issuing the following commands in
the VM:

```
$ mkdir ~/github
$ cd ~/github/
$ git clone http://git.veripool.org/git/verilator -b v4.010
$ cd verilator
$ autoconf
$ ./configure
$ make
$ sudo make install
```

## Build libsystemctlm-soc PCIe RTL VFIO demos in the Ubuntu LTS 20.04 VM

Clone and build libsystemctlm-soc's PCIe VFIO demos by issuing the
following commands in the VM:

```
$ cd ~/github/
$ git clone https://github.com/Xilinx/libsystemctlm-soc.git
$ cd libsystemctlm-soc
$ cat <<EOF > .config.mk
SYSTEMC = /opt/systemc-2.3.2/
EOF
$ cd tests/rtl-bridges/pcie
$ make
```

Below two VFIO test demos will be found inside the directory after the
build:

```
$ ls test-pcie-ep-master-vfio test-pcie-ep-slave-vfio 
```

## Launching QEMU and the refdesign-sim demo 

Instructions on how to launch and connect QEMU (running above Ubuntu VM) with 
the PCIe EP in the refdesign-sim demo can be found inside:
[../../../docs/pcie-rtl-bridges/overview.md](../../../docs/pcie-rtl-bridges/overview.md).

## Exercising the refdesign-sim demo with VFIO test applications

After connecting the PCIe EP in the refdesign-sim with QEMU it is possible to
run the VFIO tests demos inside the Ubuntu VM for exercising the EP. This is
done by issuing below commmands inside the VM (the commands are explained in
more detail inside [../axi/README.md](../axi/README.md)):

```
$ cd ~/github/libsystemctlm-soc/tests/rtl-bridges/pcie
$ sudo modprobe vfio-pci nointxmask=1
$ sudo sh -c 'echo 10ee d004 > /sys/bus/pci/drivers/vfio-pci/new_id'
$ # Find the iommu group
$ ls -l /sys/bus/pci/devices/0000\:01\:00.0/iommu_group
$ # The iommu group was 3
$ sudo ./test-pcie-ep-master-vfio 0000:01:00.0 3 0

        SystemC 2.3.2-Accellera --- Jan  8 2021 14:33:28
        Copyright (c) 1996-2017 by all Contributors,
        ALL RIGHTS RESERVED
Device supports 9 regions, 5 irqs
mapped 0 at 0x7fb2352b3000

Info: (I702) default timescale unit used for tracing: 1 ps (./test-pcie-ep-master-vfio.vcd)
Bridge ID c3a89fe1
Position 0
version=100
type=12 pcie-axi-master
Bridge version 1.0
Bridge data-width 128
Bridge nr-descriptors: 16
--------------------------------------------------------------------------------
[15 us]

Write : 0x0, length = 16384, streaming_width = 16384

data = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0....
```

A VCD file (refdesign-sim.vcd) will be created in the host directory from where
the refdesign-sim demo was launched after running the test. That VCD file can
be inspected with gtkwave. Example command lines for installing and launching
gtkwave on a Debian / Ubuntu system are found below. 

```
$ sudo apt-get install gtkwave
$ gtkwave refdesign-sim.vcd
```
