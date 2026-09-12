from pathlib import Path
root = Path(__file__).resolve().parents[1]
ctrl = (root / "src/kext/AirportRTW88.cpp").read_text()
mgr = (root / "src/kext/RTW88AWDLManager.cpp").read_text()
hdr = (root / "src/kext/RTW88AWDLManager.hpp").read_text()
awdl = (root / "src/kext/AirportRTW88AWDL.cpp").read_text()

def body(start, end):
    a = ctrl.index(start)
    b = ctrl.index(end, a + len(start))
    return ctrl[a:b]

stop = body("void AirportRTW88::stop(IOService *provider)", "void AirportRTW88::free()")
assert stop.index("super::stop(provider)") < stop.index("teardown();"), "super::stop must precede teardown"
fail = body("bool AirportRTW88::failStart", "bool AirportRTW88::setupInterrupt")
assert fail.index("super::stop(provider)") < fail.index("teardown();"), "failStart must stop super before teardown"
assert "capacity < _syncTemplateLength" in mgr
assert "kIOReturnNoSpace" in mgr
assert "d->payload_len = length" in awdl
assert "class IO80211VirtualInterface;" in hdr
assert "IO80211VirtualInterface.h" not in hdr
assert "IO80211Controller.h" in mgr and "IO80211VirtualInterface.h" in mgr
for token in [
    "capabilities[2] = 0xFF", "capabilities[3] = 0x2B",
    "capabilities[4] = 0xAD", "capabilities[5] = 0x8C",
    "capabilities[6] = 0x8C", "capabilities[7] = 0x84",
]:
    assert token in ctrl, token
print("1.0.1 preload safety: OK")
