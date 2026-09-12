from pathlib import Path

cpp = (Path(__file__).resolve().parents[1] / "src/kext/AirportRTW88.cpp").read_text()
awdl = (Path(__file__).resolve().parents[1] / "src/kext/AirportRTW88AWDL.cpp").read_text()

start = cpp.index("void AirportRTW88::setLinkStatus(UInt32 status)")
end = cpp.index("void AirportRTW88::rtw88Event", start)
body = cpp[start:end]

# Infrastructure link transitions must never force the AWDL VIF up/down.
assert "awdlInterface()" not in body
assert "setEnabledBySystem" not in body
assert "awdl->setLinkState" not in body

# The AWDL VIF owns its state through its own lifecycle instead.
assert "enableVirtualInterface" in awdl
assert "interface->setEnabledBySystem(true)" in awdl
assert "interface->setLinkState(kIO80211NetworkLinkUp, 0)" in awdl
assert "disableVirtualInterface" in awdl
assert "interface->setLinkState(kIO80211NetworkLinkDown, 0)" in awdl
