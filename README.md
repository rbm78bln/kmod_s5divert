# kmod_s5divert

An ACPI kernel module to divert system power off from S5 to S4, S3, or system reboot.

Many ACPI systems can only wake from the S5 (soft off) state using the power button. However, this limitation usually doesn’t apply to S4 (suspend to disk) or S3 (suspend to RAM). These states often support several additional wakeup sources, though they consume slightly more power while suspended. Unfortunately, when you shut down your system normally, it typically enters S5, making it impossible to wake from any other source.

This kernel module prevents the system from entering the S5 state once loaded. Instead, it transitions the system into S4, S3, or reboots it, depending on configuration.
If your ACPI wakeup sources are configured correctly, the system can wake from any enabled trigger and will boot up as if it had been started from a cold power-on.


```shell
$ modinfo s5divert

name:           s5divert
description:    Divert system power off from ACPI S5 to S4, S3, or system reboot
filename:       /lib/modules/linux/extramodules/s5divert.ko
version:        0.1
license:        GPL
author:         rbm78bln
parm:           enabled: Enable/disable diversion of ACPI S5 to either
		   0: diversion disabled
		   1: ACPI state S4 (without saving) [default]
		   2: ACPI state S3 (without return vector)
		   3: ACPI state S0 (reboot)
parm:           poweroff: Instantly power off the system. Default: 0
parm:           reboot: Instantly reboot the system. Default: 0
parm:           stroff: Instantly enter ACPI state S3 and reboot the system right away after waking up. Default: 0
```

## Parameters in detail
The module’s behavior can be configured at load time or adjusted later while it is running.

### Parameter "enabled"
This parameter is used to enable or disable the module at load time or runtime.

#### enabled = 0
This effectively disables the module, allowing the system to enter the ACPI S5 state normally.

#### enabled = 1
When the system is about to enter the ACPI S5 state, the module takes over control and instead puts the system into S4 without writing the current system state to disk, keeping all previously configured wakeup sources active.

This is the default behavior.

#### enabled = 2
When the system is about to enter the ACPI S5 state, the module takes over control and instead puts the system into S3 without defining a return vector for wake-up, while keeping all previously configured wakeup sources active.

The actual behavior upon wake-up depends heavily on your system’s firmware implementation: many systems will perform a fresh boot, while others may crash or behave unpredictably. You will need to test this on your specific hardware.

Note that this state consumes significantly more power while suspended.

#### enabled = 3
When the system is about to enter the ACPI S5 state, the module takes over control and instead forces an immediate system reboot. ACPI wakeup sources do not apply in this mode. This effectively prevents the machine from being powered off.

## Triggers in detail
Triggers are intended to be invoked from within your own custom scripts located in ```/usr/lib/systemd/system-shutdown/```. This allows you to redirect or modify the system’s behavior during the shutdown sequence handled by systemd. Writing ```1```, ```y```, or ```true``` to a trigger activates it, while reading from it always returns ```0``` without performing any action. Writing ```0```, ```n```, or ```false``` to it won’t perform any action either. If a trigger is activated via a module parameter at load time, the system will not return from the load operation but will execute the trigger action immediately.

### Trigger "poweroff"
When this trigger is activated, the module immediately calls ```kernel_power_off()```. Any previously configured S5 redirection defined by the ```enabled``` parameter then takes effect. If you set this trigger while loading the module, ensure that ```enabled=<x>``` is specified before the ```poweroff=1``` parameter on the module’s command line.

### Trigger "reboot"
When this trigger is activated, the module immediately calls ```kernel_restart()```. If you set this trigger while loading the module, the system will not return from the load operation, just as when using the poweroff trigger.

### Trigger "stroff"
This trigger behaves differently from the others. Since some systems cannot reliably resume from ACPI S3 state when no return vector is defined (as in ```enabled=2```), this trigger enters S3 with a proper return vector and then reboots the system immediately after waking up.

When this trigger is activated, the module forces the system to enter ACPI S3 state immediately and automatically reboots once it resumes from that state. So technically speaking, the system does return from suspend-to-RAM correctly, but it will reboot immediately afterward regardless.

Because ACPI S3 state cannot be entered cleanly while the kernel is already shutting down, this trigger cannot be used as a redirection target via the ```enabled``` parameter. However, correct operation of this trigger does not depend on the system’s firmware implementation so much.

Note that this state consumes significantly more power while suspended.

## Parameters and triggers at runtime

Once loaded, parameters and triggers are exposed in ```/proc``` and ```/sys``` for convenience and runtime configuration:

```shell
$ sudo insmod ./s5divert.ko enabled=1
$ find /proc /sys -path '*/s5divert/*' | xargs '-d\n' ls -l
-rw-rw-r-- 1 root root /proc/s5divert/enabled
--w--w---- 1 root root /proc/s5divert/poweroff
--w--w---- 1 root root /proc/s5divert/reboot
--w--w---- 1 root root /proc/s5divert/stroff

-rw-rw-r-- 1 root root /sys/kernel/s5divert/enabled
--w--w---- 1 root root /sys/kernel/s5divert/poweroff
--w--w---- 1 root root /sys/kernel/s5divert/reboot
--w--w---- 1 root root /sys/kernel/s5divert/stroff

-rw-rw-r-- 1 root root /sys/module/s5divert/parameters/enabled
--w--w---- 1 root root /sys/module/s5divert/parameters/poweroff
--w--w---- 1 root root /sys/module/s5divert/parameters/reboot
--w--w---- 1 root root /sys/module/s5divert/parameters/stroff
```

## Wakeup sources

For this kernel module to work correctly, make sure the ACPI wakeup sources are configured properly in ```/proc/acpi/wakeup```:

```shell
$ cat /proc/acpi/wakeup
Device  S-state   Status   Sysfs node
ADP1      S4    *enabled   platform:ACPI0003:00
ARPT      S4    *disabled  pci:0000:03:00.0
EC        S4    *enabled   platform:PNP0C09:00
HDEF      S3    *disabled  pci:0000:00:1b.0
LID0      S4    *enabled   platform:PNP0C0D:00
XHC1      S3    *enabled   pci:0000:00:14.0
```

## How to build a GNU Make based project

```shell
# Build project
$ make

# Check module
$ modinfo ./s5divert.ko
[some gibberish]

# Clean build files
$ make clean
```

## How to build and install the Arch Linux DKMS package

```shell
# Build package
$ make aur

# Install package
$ sudo pacman -U kmod_s5divert-dkms*.pkg.tar*

# Clean build files
$ make clean
```
![-](https://miunske.eu/github/?s5divert)
