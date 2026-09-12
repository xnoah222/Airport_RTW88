# Changelog

## 1.0.1

- Expanded PCIe device-ID coverage across the supported RTL88xx family mappings.
- Added/updated embedded firmware coverage for the enabled PCIe chipsets.
- Fixed overlapping CoreWiFi scans by coalescing requests instead of returning busy/error 16.
- Corrected the bundled Ventura `apple80211_scan_result` ABI to match the layout expected by airportd.
- Retains the stable native AirPort/IO80211 STA path from 1.0.0.
- Retains CURRENT_NETWORK / Location Services compatibility work from the late 1.0.0 development branch.
- AWDL/P2P code remains experimental and is not a supported 1.0.1 feature.

### Hardware validation

RTL8822BE is the physically validated adapter. Other enabled PCIe IDs remain experimental until confirmed on real hardware.
