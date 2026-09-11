from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "src/kext/AirportRTW88.cpp").read_text()
hpp = (root / "src/kext/AirportRTW88.hpp").read_text()
ieh = (root / "src/kext/RTW88IEEE80211.hpp").read_text()
iec = (root / "src/kext/RTW88IEEE80211.cpp").read_text()

assert "case APPLE80211_IOC_CURRENT_NETWORK:" in cpp
assert "handleCURRENT_NETWORK(static_cast<apple80211_scan_result *>(data))" in cpp
assert "fillScanResultFromBSS(current, out, true)" in cpp
assert "fillScanResultFromBSS(b, &_scanResult, false)" in cpp
assert "copyCurrentBSS(RTW88BSS *out)" in ieh
assert "RTW88IEEE80211::copyCurrentBSS" in iec
assert "_state != RTW88_STATE_CONNECTED" in iec
assert "out->rssi = (int16_t)_rssi" in iec
print("CURRENT_NETWORK contract: OK")
