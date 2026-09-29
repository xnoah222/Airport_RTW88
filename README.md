AirPort_RTW88

[!WARNING]
This repository is deprecated and will no longer receive updates.

The releases available here are early and experimental versions of AirPort_RTW88. They contain stability and connectivity issues that have since been addressed during continued development.

These releases no longer represent the current state of AirPort_RTW88 and are not recommended for regular use.

A new project is coming

Development is currently moving to a new unified project:

Realtek AirPort Family for macOS

Realtek AirPort Family for macOS is being developed as the successor to this repository.

Instead of targeting only modern versions of macOS, the new project brings Realtek Wi-Fi support across multiple generations of Apple’s AirPort networking stack through dedicated drivers.

macOS compatibility

macOS	Version	Current status
Mojave	10.14	✅ Working
Catalina	10.15	✅ Working
Big Sur	11.0 – 11.7.11	✅ Working
Monterey	12.x	🚧 In development
Ventura	13.7.7 – 13.7.8	✅ Working
Sonoma	14.4 – 14.8.x	✅ Working
Sequoia	15.0 – 15.8.x	✅ Working
Tahoe	26.0 – 26.7	✅ Working

Different macOS generations use different versions of Apple’s Wi-Fi frameworks. Because of this, Realtek AirPort Family uses dedicated driver implementations where necessary instead of forcing a single driver across every supported system.

Driver family

The project currently consists of three branches:

Realtek88LegacyAirport
Designed for macOS Mojave through Big Sur.

RTL88_Airport21
Designed specifically for macOS Monterey / Darwin 21.
This driver is currently under development.

AirPort_RTW88
Designed for macOS Ventura and newer.

AirPort_RTW88 has continued development beyond the experimental releases available in this repository. Core Wi-Fi functionality has received major stability improvements, including:

* Wi-Fi scanning and association
* Stable network connectivity
* Sustained network traffic
* Switching between Wi-Fi networks
* Switching between 2.4 GHz and 5 GHz networks
* Sleep and wake recovery

Core Wi-Fi functionality in the current AirPort_RTW88 codebase is considered stable.

AWDL support remains experimental and is not part of the core Wi-Fi stability guarantee.

What happens to this repository?

This repository contains the original experimental development history of AirPort_RTW88.

It will remain available as an archive, but no further releases or fixes are planned here.

Issues encountered while using binaries from this repository may already have been resolved in the newer driver codebase. For this reason, bug reports based on these old releases are no longer recommended.

Once publicly available, Realtek AirPort Family for macOS will become the official home for new releases, compatibility information, documentation and source code.

⸻

Realtek AirPort Family for macOS — currently in development.

Bringing Realtek Wi-Fi support across generations of macOS.
