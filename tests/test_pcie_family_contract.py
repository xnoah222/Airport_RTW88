#!/usr/bin/env python3
from pathlib import Path
import plistlib, re

ROOT = Path(__file__).resolve().parents[1]
PLIST = ROOT / "AirPort_RTW88.kext/Contents/Info.plist"
MAKEFILE = (ROOT / "Makefile").read_text()
SRC = (ROOT / "src/kext/RTW88IEEE80211.cpp").read_text()

EXPECTED = {
    "AirPort_RTW88_8822BE": {"0xB82210EC"},
    "AirPort_RTW88_8822CE": {"0xC82210EC", "0xC82F10EC"},
    "AirPort_RTW88_8821CE": {"0xC82110EC", "0xB82110EC"},
    "AirPort_RTW88_8821AE": {"0x882110EC"},
    "AirPort_RTW88_8812AE": {"0x881210EC"},
    "AirPort_RTW88_8814AE": {"0x881310EC"},
}

with PLIST.open("rb") as f:
    p = plistlib.load(f)
pers = p["IOKitPersonalities"]
assert set(pers) == set(EXPECTED), (set(pers), set(EXPECTED))
for name, ids in EXPECTED.items():
    d = pers[name]
    assert d["IOProviderClass"] == "IOPCIDevice"
    assert set(d["IOPCIMatch"].split()) == ids
    assert "RTW88ChipName" not in d and "RTW88FWName" not in d

# The AirPort build must not compile transport/frontends outside PCIe.
for forbidden in [
    "$(LINUX_SRC)/usb.c", "$(LINUX_SRC)/sdio.c",
    "$(LINUX_SRC)/rtw8822bu.c", "$(LINUX_SRC)/rtw8822cu.c",
    "$(LINUX_SRC)/rtw8821cu.c", "$(LINUX_SRC)/rtw8812au.c",
    "$(LINUX_SRC)/rtw8814au.c", "$(LINUX_SRC)/rtw8821au.c",
    "CONFIG_RTW88_8821AU", "CONFIG_RTW88_8822BU", "CONFIG_RTW88_8822CU", "CONFIG_RTW88_8812AU",
]:
    assert forbidden not in MAKEFILE, forbidden
assert "$(LINUX_SRC)/pci.c" in MAKEFILE
assert "CONFIG_RTW88_PCI=1" in MAKEFILE

# Every plist PCI device id must exist in the C++ device-id -> hw_spec table.
for ids in EXPECTED.values():
    for full in ids:
        device = full[2:6].upper()
        assert re.search(r"\{\s*0x%s\s*,\s*&rtw\w+_hw_spec\s*\}" % device, SRC), full

print("PASS: PCIe family contract")


legacy = plistlib.load((ROOT / "rtw88.kext/Contents/Info.plist").open("rb"))
assert all(x.get("IOProviderClass") == "IOPCIDevice" for x in legacy["IOKitPersonalities"].values())
assert not (ROOT / "src/kext/RTW88USBDevice.cpp").exists()
assert not (ROOT / "src/kext/RTW88USBDevice.hpp").exists()
assert "RTW88USBDevice" not in (ROOT / "rtw88.xcodeproj/project.pbxproj").read_text()
