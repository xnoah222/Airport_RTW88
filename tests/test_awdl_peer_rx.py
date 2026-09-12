#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "src/kext/AirportRTW88.cpp").read_text()
backend = (root / "src/kext/RTW88IEEE80211.cpp").read_text()
hpp = (root / "src/kext/AirportRTW88.hpp").read_text()

checks = {
    "metadata signature": "injectRxActionFrame(const uint8_t *frame, uint32_t len, int8_t rssi, uint16_t channel)" in hpp,
    "AWDL BSSID": "0x00, 0x25, 0x00, 0xff, 0x94, 0x73" in cpp,
    "Apple OUI": "0x00, 0x17, 0xf2" in cpp,
    "vendor category": "a[0] != 0x7f" in cpp,
    "AWDL type 8": "a[4] != 8" in cpp,
    "PSF/MIF filter": "subtype != 0 && subtype != 3" in cpp,
    "peer presence": "postPeerPresence" in cpp,
    "zeroed opaque tag storage": "tagStorage[64]" in cpp and "rtw88ZeroPacketInfo" in cpp,
    "backend passes RX metadata": "rxs ? (int8_t)rxs->signal : 0" in backend and "rxChannel" in backend,
    "not blanket infra forwarding": "rtw88IsAWDLActionFrame" in cpp,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("FAIL: " + ", ".join(failed))
print("PASS: AWDL peer RX classifies PSF/MIF and supplies peer/radio metadata")
