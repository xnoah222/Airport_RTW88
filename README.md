# Feixiao

Feixiao is a macOS kernel extension that ports the Linux `rtw88` driver, bringing support for Realtek PCIe Wi-Fi cards to macOS.

## Supported Hardware

Feixiao aims to support the following Realtek chipsets:
- RTL8822BE
- RTL8822CE
- RTL8821CE
- RTL8812AE
- RTL8814AE

*Note: USB and SDIO variants are unsupported, and are not planned to be supported.*

## Features

- Native scanning of 2.4GHz and 5GHz bands.
- Connection to Open and WPA/WPA2 (incl. mixed modes) (CCMP/PSK/TKIP/AES/TKIP+AES) networks.
- Custom `rtw88ctl` command-line utility to control the driver, with possibility to use [Starskiff](https://github.com/thegwchr/Starskiff) as GUI alternative

## Build Instructions

To build Feixiao from source, you will need the rtw88 driver from Linux kernel source, Xcode Command Line Tools and the macOS Kernel SDK.

```sh
# Clone the repository
git clone https://github.com/thegwchr/Feixiao.git
cd Feixiao
git submodule update --recursive
```

Before building Feixiao, clone the Linux `rtw88` driver source from thegwchr/rtw88-stable:

```sh
git clone https://github.com/thegwchr/rtw88-stable.git
```

Directory structure must be:

```
parent/
├── Feixiao/        # this repository
└── rtw88-stable/   # Linux kernel tree root (driver at drivers/net/wireless/realtek/rtw88/)
```

If you name the clone differently, update `LINUX_SRC` in `Makefile` accordingly.


```sh
# Build the kext and control utility
make all
```

The compiled kernel extension (`rtw88.kext`) and the command-line utility (`rtw88ctl`) will be generated in the `build/out/` directory.


## Native AirPort target — AirPort_RTW88 1.0.0

The repository also contains the native IO80211 target used by macOS Wi-Fi UI:

```sh
make airport
```

Output:

```text
build/out/AirPort_RTW88.kext
```

`AirPort_RTW88 1.0.0` keeps the proven internal C++ class names (`AirportRTW88*`) and bundle identifier (`com.rtw88.airport`) while changing the visible kext/executable name. This release adds the Ventura AWDL/P2P virtual-interface bridge, verified AWDL control IOCTLs, and raw 802.11 management/action-frame TX/RX plumbing. See `AWDL_1.0.0_NOTES.md` for the first-boot trace and current off-channel limitation.

## Installation & Usage

1. **Load the Kext:**
   To load it manually for testing:
   ```sh
   sudo chown -R root:wheel build/out/rtw88.kext
   sudo kextload build/out/rtw88.kext
   ```
   *(You may be prompted to approve the extension in System Settings -> Privacy & Security. If you are using a Hackintosh, you can inject it via OpenCore).*

2. **Check Status:**
   Use the built-in control utility to check if the driver successfully initialized your card:
   ```sh
   ./build/out/rtw88ctl status
   ```

3. **Scan for Networks:**
   ```sh
   # Trigger a hardware scan
   ./build/out/rtw88ctl scan
   
   # List the discovered access points
   ./build/out/rtw88ctl list
   ```

4. **Connect to a Network:**
   ```sh
   ./build/out/rtw88ctl connect "Your_SSID" "Your_Password"
   ```

## Troubleshooting

You can view driver logs using `dmesg` if you're injecting it as pre-link (OpenCore):
```sh
dmesg | grep -i rtw88
```

or, if you're loading via `kextload/kmutil`:

```sh
log stream --predicate 'process == "kernel" and (eventMessage contains "rtw88" or message contains "rtw88")' --info
```

If you encounter kernel panics or connection failures, ensure you are using the latest commit and that your specific card's PCI ID is bound to the driver.

## Acknowledgements

- **Realtek** for the original Linux `rtw88` driver source code and WLAN card;
- **Apple** for the OS;
- **AcidAnthera** for MacKernelSDK;
- **OpenIntelWireless** for itlwm as reference;
- **FreeBSD** for LinuxKPI as reference on how Linux drivers get implemented in BSD.
