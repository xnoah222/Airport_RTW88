#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "src/kext/AirportRTW88AWDL.cpp").read_text()
hdr = (root / "MacKernelSDK/Headers/IOKit/80211/IO80211P2PInterface.h").read_text()

assert "class IO80211P2PInterface : public IO80211VirtualInterface" in hdr
assert "new IO80211P2PInterface" in src
assert "p2p->init(this, addr, role, rtw88VifRoleName(role))" in src
assert "new IO80211VirtualInterface" not in src
assert "super::enableVirtualInterface(interface)" in src
assert "interface->setEnabledBySystem(true)" in src
assert "interface->setLinkState(kIO80211NetworkLinkUp, 0)" in src
assert "interface->postMessage(APPLE80211_M_LINK_CHANGED)" in src
print("PASS: AWDL/P2P roles are constructed as IO80211P2PInterface")
