# AirPort_RTW88

AirPort_RTW88 is a native macOS AirPort Wi-Fi driver for selected Realtek RTL88xx PCIe wireless adapters.

It ports the Linux `rtw88` driver to macOS and integrates it with Apple's native AirPort/IO80211 Wi-Fi stack, allowing supported Realtek adapters to appear and operate through the built-in macOS Wi-Fi interface.

AirPort_RTW88 uses the Linux `rtw88` driver as its hardware backend, with macOS integration inspired by and adapted from OpenIntelWireless `itlwm` / `AirportItlwm`.

> [!IMPORTANT]
> AirPort_RTW88 is still under active development.
>
> Standard Wi-Fi connectivity is functional on supported hardware.  
> **AWDL and AirDrop are not functional yet.**

## Current Status

**Current release: AirPort_RTW88 1.0.2**

Version 1.0.2 is a hardware compatibility, network stability and modern macOS compatibility release.

Currently working:

- Native AirPort/IO80211 interface
- Wi-Fi network scanning
- 2.4 GHz networks
- 5 GHz networks
- Association and connection through the native macOS Wi-Fi interface
- Open networks
- WPA/WPA2 networks
- Normal IP network traffic after association
- RTL8822BE support
- RTL8822CE support
- RTL8821CE support
- macOS Ventura 13.7.7+
- macOS Sonoma 14.4+ through Tahoe 26 using the Legacy IO80211 stack

### Experimental development code

The source tree contains ongoing AWDL/P2P development used for AirDrop and Continuity research.

Although AWDL-related classes, files and infrastructure are present in the source code, **AWDL is not functional in AirPort_RTW88 1.0.2**.

The following features should therefore be considered unsupported:

- AirDrop
- AWDL
- Handoff
- Universal Clipboard
- Other AWDL-dependent Continuity functionality

Functional AWDL/AirDrop support is planned for a future release.

---

## Supported Hardware

AirPort_RTW88 1.0.2 officially supports:

| Chipset | PCI Device IDs | Status |
| --- | --- | --- |
| **RTL8822BE** | `10EC:B822` | Supported |
| **RTL8822CE** | `10EC:C822`, `10EC:C82F` | Supported |
| **RTL8821CE** | `10EC:C821`, `10EC:B821` | Supported |

Other RTL88xx chipsets are currently **not officially supported**.

> **PCIe only:** USB and SDIO Realtek wireless adapters are intentionally excluded from the AirPort target. Upstream source files for other transports may remain in the vendored `rtw88` source tree, but they are not part of the supported AirPort_RTW88 configuration.

---

## macOS Compatibility

| macOS | Status | Configuration |
| --- | --- | --- |
| Ventura 13.7.7+ | Supported | Native Ventura configuration |
| Sonoma 14.4+ | Supported | Legacy Stack + AMFIPass |
| Sequoia 15.x | Supported | Legacy Stack + AMFIPass |
| Tahoe 26.x | Supported | Legacy Stack + AMFIPass |

### macOS Ventura

AirPort_RTW88 directly supports **macOS Ventura 13.7.7 and newer**.

The Legacy Stack must **NOT** be installed on Ventura.

Do not inject:

```text
IOSkywalkFamily.kext
IO80211FamilyLegacy.kext
```

as part of the Sonoma+ Legacy configuration when using Ventura.

Using the Legacy Stack incorrectly on Ventura may cause system inconsistencies, boot problems or kernel panics.

Use the Ventura-specific configuration included in the release package.

### macOS Sonoma / Sequoia / Tahoe

Starting with **macOS Sonoma 14.4**, AirPort_RTW88 operates through the Legacy IO80211 networking stack.

The following kexts are required:

```text
AirPort_RTW88.kext
AMFIPass.kext
IOSkywalkFamily.kext
IO80211FamilyLegacy.kext
```

Without the required Legacy Stack configuration, AirPort_RTW88 will not load correctly.

The AirPort_RTW88 release package contains separate configurations for Ventura and Sonoma 14.4 through Tahoe 26.

> [!WARNING]
> Do not use the Sonoma+ Legacy Stack configuration on macOS Ventura.

### OpenCore / OCAT Configuration

Sonoma, Sequoia and Tahoe require additional OpenCore configuration for the Legacy networking stack.

**[INSERT KERNEL -> ADD OCAT SCREENSHOT]**

**[INSERT KERNEL -> BLOCK OCAT SCREENSHOT]**

Follow the configuration shown in the screenshots carefully before rebooting.

---

# What's New in AirPort_RTW88 1.0.2

## RTL8822CE and RTL8821CE Support

AirPort_RTW88 is no longer limited to RTL8822BE.

Version 1.0.2 adds working support for:

- RTL8822CE
- RTL8821CE

PCI compatibility, device initialization and driver handling for these chipsets have been corrected.

Together with RTL8822BE, these are now the three officially supported AirPort_RTW88 chipsets.

## Network Degradation Fix — Ventura through Tahoe

Version 1.0.2 fixes an important issue that could cause network performance to progressively degrade during use.

Previously, a connection could initially operate normally but gradually lose throughput and stability, particularly during sustained or heavier network traffic.

Traffic handling and connection stability have been improved across the entire supported macOS range:

**Ventura → Sonoma → Sequoia → Tahoe**

This is a general AirPort_RTW88 driver fix and is not limited to the Sonoma+ Legacy Stack.

## Sonoma through Tahoe Fixes

AirPort_RTW88 1.0.2 also contains fixes specifically targeting operation through the Legacy IO80211 stack on newer macOS releases.

### Network Association

Fixed an issue where AirPort_RTW88 could successfully scan and display nearby Wi-Fi networks but fail to properly associate with them.

Association handling has been corrected for the Legacy Stack configuration used on:

- macOS Sonoma
- macOS Sequoia
- macOS Tahoe

### Legacy IO80211 Compatibility

Improved compatibility between AirPort_RTW88 and:

```text
IOSkywalkFamily.kext
IO80211FamilyLegacy.kext
```

This allows the Ventura-based AirPort_RTW88 implementation to operate on newer macOS versions through the restored Legacy networking stack.

Additional request-handling and compatibility fixes have been implemented for Sonoma through Tahoe.

---

## Installation

AirPort_RTW88 is intended primarily for use through OpenCore.

Download the latest package from the GitHub Releases section.

The 1.0.2 release contains separate configurations for:

```text
Airport_RTW88 for Ventura/

Airport_RTW88 for Sonoma 14.4 - Tahoe 26/
```

### Ventura 13.7.7+

Add:

```text
AirPort_RTW88.kext
```

to:

```text
EFI/OC/Kexts/
```

and add the corresponding entry to `Kernel -> Add` in `config.plist`.

Do not install the Legacy Stack on Ventura.

### Sonoma 14.4+ / Sequoia / Tahoe

The following components are required:

```text
AirPort_RTW88.kext
AMFIPass.kext
IOSkywalkFamily.kext
IO80211FamilyLegacy.kext
```

Add the required kexts to OpenCore and configure `Kernel -> Add` / `Kernel -> Block` according to the OCAT screenshots provided in this repository.

Incorrect Legacy Stack configuration may prevent AirPort_RTW88 from loading or may cause boot problems.

---

## Building

AirPort_RTW88 requires:

- Xcode Command Line Tools
- MacKernelSDK
- Linux `rtw88` source

Clone the repository and its submodules:

```sh
git clone --recursive https://github.com/xnoah222/Airport_RTW88.git
cd Airport_RTW88
```

If the repository was cloned without `--recursive`:

```sh
git submodule update --init --recursive
```

AirPort_RTW88 expects the Linux `rtw88` source tree in the location configured by the project's build system.

If using the external `rtw88-stable` tree, the expected layout is:

```text
parent/
├── Airport_RTW88/
└── rtw88-stable/
```

with the Linux driver source available at:

```text
../rtw88-stable/drivers/net/wireless/realtek/rtw88/
```

If a different location is used, update the appropriate source path in the build configuration.

Build AirPort_RTW88 with:

```sh
make airport
```

The resulting kernel extension is normally generated under:

```text
build/out/AirPort_RTW88.kext
```

> [!NOTE]
> The development tree may evolve between releases. Check the current Makefile and repository structure when building directly from source.

---

## Troubleshooting

Driver messages can be inspected with:

```sh
log show --last boot --predicate 'process == "kernel"' | grep -Ei 'rtw88|AirportRTW88'
```

For live debugging:

```sh
log stream --predicate 'process == "kernel"' --info | grep -Ei 'rtw88|AirportRTW88'
```

Useful messages include:

- Firmware loading
- PCI initialization
- Wi-Fi scanning
- Association
- RSN/EAPOL state
- Network-interface initialization

If the adapter is not detected or AirPort_RTW88 does not load, verify that:

- AirPort_RTW88.kext is loaded
- The PCI device is visible to macOS
- The adapter is an RTL8822BE, RTL8822CE or RTL8821CE
- The correct configuration for your macOS version is being used
- OpenCore is injecting the required kexts correctly
- Sonoma and newer have AMFIPass and the Legacy Stack correctly configured

When reporting Sonoma, Sequoia or Tahoe problems, verify the Legacy Stack configuration before opening an issue.

---

## Project Structure

AirPort_RTW88 consists primarily of three layers:

- The ported Linux `rtw88` hardware / PHY / MAC implementation
- The macOS AirPort / IO80211 STA integration layer
- The experimental `RTW88AWDLManager` / virtual-interface infrastructure used for ongoing AWDL/P2P development

The macOS layer exposes supported Realtek hardware as a native AirPort Wi-Fi interface rather than presenting it as an Ethernet adapter.

Some internal class names retain the `AirportRTW88` naming used during development.

AWDL-related code in the source tree should not be interpreted as functional AWDL support in the current release.

---

## Known Limitations

AirPort_RTW88 1.0.2 currently has the following limitations:

- AWDL is not functional
- AirDrop is not functional
- Handoff and Universal Clipboard are not supported
- Other AWDL-dependent Continuity functionality is not supported
- Only RTL8822BE, RTL8822CE and RTL8821CE are officially supported
- USB Realtek Wi-Fi adapters are not supported
- SDIO Realtek Wi-Fi adapters are not supported
- Sonoma 14.4 and newer require the Legacy IO80211 stack and AMFIPass

---

## Reporting Issues

When opening an issue, please provide:

- Realtek chipset
- PCI device ID
- macOS version
- OpenCore version
- AirPort_RTW88 version
- Ventura or Legacy Stack configuration
- Relevant AirPort_RTW88 / `rtw88` kernel logs

Please do not report AWDL/AirDrop as a 1.0.2 Wi-Fi connectivity bug. AWDL is currently under development.

Reports involving unsupported RTL88xx hardware may not be actionable.

---

## Acknowledgements

AirPort_RTW88 would not be possible without work from several open-source projects and communities:

- **Linux rtw88 contributors** — for the original Realtek RTL88xx driver implementation
- **OpenIntelWireless** — for `itlwm` and `AirportItlwm`, which served as important references for macOS Wi-Fi and native AirPort integration
- **Acidanthera** — for MacKernelSDK and its contributions to the Hackintosh development ecosystem
- **OpenCore Legacy Patcher contributors** — for their work surrounding restored Legacy wireless frameworks on newer macOS versions
- **Apple** — for macOS, IO80211, IOSkywalkFamily and the surrounding networking frameworks
- **FreeBSD** — for LinuxKPI and its work adapting Linux driver concepts to BSD environments

See `CREDITS.md`, upstream projects and individual source files for additional attribution, copyright notices and licensing information.

---

## Disclaimer

AirPort_RTW88 is an independent open-source project.

It is not affiliated with, endorsed by, sponsored by or supported by Apple, Realtek, OpenIntelWireless, Acidanthera, OpenCore Legacy Patcher or the Linux kernel project.

Use AirPort_RTW88 at your own risk.

Kernel extensions operate with high privileges. Incorrect installation, unsupported hardware, incompatible system configurations or experimental functionality may cause system instability, loss of networking functionality or kernel panics.

Always keep a working OpenCore/EFI backup before modifying your wireless configuration.

---

## License

AirPort_RTW88 contains and adapts code originating from multiple open-source projects.

Licensing and copyright notices present in individual source files must be preserved.

See `CREDITS.md`, upstream projects and the relevant source files for attribution and licensing details.
