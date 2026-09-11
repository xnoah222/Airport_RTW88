from pathlib import Path

root = Path(__file__).resolve().parents[1]
awdl = (root / "src/kext/AirportRTW88AWDL.cpp").read_text()
main = (root / "src/kext/AirportRTW88.cpp").read_text()

for token in [
    "APPLE80211_IOC_P2P_SCAN",
    "APPLE80211_IOC_P2P_LISTEN",
    "APPLE80211_IOC_P2P_GO_CONF",
    "APPLE80211_IOC_AWDL_SYNC_PARAMS",
    "APPLE80211_IOC_AWDL_SYNC_STATE",
    "APPLE80211_IOC_AWDL_PRESENCE_MODE",
    "APPLE80211_IOC_AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE",
    "APPLE80211_IOC_AWDL_DEVICE_CAPABILITIES",
    "APPLE80211_IOC_AWDL_AF_TX_MODE",
]:
    assert token in awdl, token

assert "if (feature == kIO80211Feature80211n)" in main
assert "return 102;" in main
assert "attachVirtualInterface(slot, &addr, d->role, true)" in main
assert "txRawManagementFrame" in awdl
print("AirportItlwm AWDL alignment: OK")
