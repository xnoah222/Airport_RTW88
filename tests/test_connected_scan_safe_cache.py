from pathlib import Path
root = Path(__file__).resolve().parents[1]
a = (root / "src/kext/AirportRTW88.cpp").read_text()
b = (root / "src/kext/RTW88IEEE80211.cpp").read_text()
assert "SCAN_REQ connected cache-only completion" in a
assert "SCAN_REQ_MULTIPLE connected cache-only completion" in a
assert "st.state == RTW88_STATE_CONNECTED" in a
assert "_netif->postMessage(APPLE80211_M_SCAN_DONE" in a
cmd = b.split("IOReturn RTW88IEEE80211::cmdScan()",1)[1].split("void RTW88IEEE80211::manualScanTCFn",1)[0]
assert "if (_state != RTW88_STATE_IDLE)" in cmd
assert "_state != RTW88_STATE_IDLE && _state != RTW88_STATE_CONNECTED" not in cmd
print("PASS: connected scans complete from cache without channel hopping")
