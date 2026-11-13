# Accessory Mount Plugin

PS Vita PCH-1000 plug-in and offline installer for PSVSD storage connected through the accessory port. The plug-in adds **Settings → Devices → PSVSD Accessory Port**. The installer uses the Vita's native controls and the blue and white microSD bubble.

The bundled driver supports firmware 3.60, 3.65, 3.67, and 3.68. The reported console test used 3.65.

## Install on your Vita

1. Download `Accessory_Mount_Plugin_0.1.6.vpk` from [Releases](https://github.com/RT-Online/PSVSD_Accessory-Port-Plugin/releases) and install it in VitaShell. You can install it over the 0.1.5 bubble. The installer needs taiHEN, **Enable Unsafe Homebrew**, and an active `ur0:tai/config.txt`. If the same config also exists in `ux0:tai`, check which copy your Vita loads before continuing.
2. Close Settings and open the **Accessory Mount Plugin** bubble. Select **Install Accessory Mount Plugin**, wait for the result, then select **Reboot**.
3. Open **Settings → Devices → PSVSD Accessory Port**. Choose **Mount as**, turn on **Enable Accessory Port**, and press **Reboot Device** at the bottom right.

**Uninstall** removes the native Settings entry and its driver files. If PSVSD provides `ux0:`, turn off **Enable Accessory Port** in Settings and reboot onto your original storage first. After uninstalling, select **Reboot** to apply the removal. The bottom-left **Back** button closes the installer.

| Mount | Use |
| --- | --- |
| `ux0:` | Main storage. Prepare VitaShell on the destination before switching. If the PSVSD/AM app is present, the plug-in also checks that a matching copy is on the destination. |
| `uma0:` | Extra storage. |
| `grw0:` | Extra storage when the game-card write slot is free. |
| `imc0:` | Extra storage when the internal memory slot is free. |

The first config edit saves `ur0:tai/config.txt.accessory-mount.bak`. The installer preserves other taiHEN entries and the saved mount selection. A fresh install leaves the boot driver inactive until you enable the accessory port in Settings.

## Moving from PSVSD/AM

[PSVSD/AM](https://github.com/RT-Online/PSVSD-AM) provides the storage tests and preparation tools. If its boot driver is installed at `ur0:tai/psvsd_am.skprx`, disable its mount, reboot onto your original storage, then back up and move that old driver out of `ur0:tai/` before using the installer. Use the native Settings page to configure `grw0:` and `imc0:`.

If your earlier native Settings build saved `xmc0:`, select a supported target and turn **Enable Accessory Port** back on.

## Manual installation

The manual ZIP includes the same plug-ins and works through VitaShell. Copy the contents of its `install/ur0/` folder to `ur0:`. Add `ur0:tai/psvsd_settings.suprx` under `*NPXS10015` in `ur0:tai/config.txt`, above YAMT's Settings entry if present and above `henkaku.suprx`.

```text
*NPXS10015
ur0:tai/psvsd_settings.suprx
```

Reboot once to load the Settings plug-in. **Enable Accessory Port** manages the kernel entry. To update an existing native Settings installation, replace the supplied files as a set and keep the existing taiHEN entries.

## Build

Install [VitaSDK](https://vitasdk.org/). To build the VPK, also install the [VitaSDK PAF component](https://github.com/Princess-of-Sleeping/vitasdk-paf-component) from commit `9a68b07aa2d7247a756765bf154f292cda20aed3`. This build used VitaSDK snapshot `sdk-snapshot-20261015.850.1` and GCC 15.2.0.

```sh
export VITASDK=/path/to/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Find the VPK in `build/installer/`. For the four plug-in binaries alone, configure with `-DACCESSORY_MOUNT_INSTALLER=OFF`. The compiled menu resource is included. To rebuild it after editing `installer/ui/installer_menu.xml`, run [psp2cxml-tool](https://github.com/Princess-of-Sleeping/psp2cxml-tool) with that XML path.

## Test and recovery

Installer 0.1.6 uses the standard VitaSDK homebrew build configuration and absolute paths for internal storage. **Reboot** requests a restart directly during normal use. When the installer detects an unfinished setup, it attempts recovery before restarting. The VPK bundles the Native Settings 0.1.3 plug-in.

If setup is interrupted, reopen the installer to finish recovery before rebooting. If a message remains, note the operation and error code it displays. Check that VitaShell can open `ur0:tai/config.txt`. Keep any `.amp-old` files and the installer journals under `ur0:data/PSVSD_AM/installer/` until recovery succeeds. The first config backup is the pre-install copy and may predate later edits. Settings diagnostics are in `ur0:data/PSVSD_AM/native-settings/settings.log`. Boot diagnostics are in `ur0:data/PSVSD_AM/kernel.log`.

## Credits and license

RefurbedTech developed the accessory-port hardware and directed console testing. AI tools assisted with development and documentation. The kernel code derives from Yifan Lu's USBMC and TheFloW's VitaShell storage work. The Settings integration follows YAMT and HENkaku plug-in conventions. [USBMC](https://github.com/yifanlu/usbmc) · [VitaShell](https://github.com/TheOfficialFloW/VitaShell) · [YAMT](https://github.com/SKGleba/yamt-vita) · [HENkaku](https://github.com/henkaku/henkaku)

Project code is GPL-3.0-or-later under [COPYING](COPYING). The taiHEN headers and linker stubs carry their own [MIT license](src/vendor/taihen/LICENSE).
