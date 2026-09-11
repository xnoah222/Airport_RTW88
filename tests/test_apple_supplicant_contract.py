"""Static WPA2/IO80211 contract checks for the Ventura audit build.

The audit intentionally keeps Apple RSN disabled because the experimental
Apple-supplicant path froze this RTL8822BE machine.  IO80211 still owns scan,
association requests and packet presentation; rtw88 performs the WPA2 4-way
handshake using the 32-byte PMK supplied in apple80211_assoc_data.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
air_h=(root/'src/kext/AirportRTW88.hpp').read_text()
air_c=(root/'src/kext/AirportRTW88.cpp').read_text()
iface=(root/'src/kext/AirportRTW88Interface.cpp').read_text()
rtw=(root/'src/kext/RTW88IEEE80211.cpp').read_text()
checks={
 'Apple RSN deliberately disabled': 'useAppleRSNSupplicant(IO80211Interface *) override { return false; }' in air_h,
 'EAPOL gets IO80211 receive path': 'ethertype == 0x888E' in iface and 'IO80211Interface::inputPacket' in iface,
 'ASSOCIATE selector handled': 'case APPLE80211_IOC_ASSOCIATE:' in air_c,
 'RSN_IE selector handled': 'case APPLE80211_IOC_RSN_IE:' in air_c,
 'AP_IE_LIST selector handled': 'case APPLE80211_IOC_AP_IE_LIST:' in air_c,
 'RSN length derived from Ventura TLV': 'd->ad_rsn_ie[1] + 2' in air_c,
 'ASSOCIATE passes PMK to internal backend': 'cmdConnect(ssid, nullptr, pmk, bssid, false)' in air_c,
 'Internal EAPOL handshake active': 'RTW88_STATE_HANDSHAKING && !_externalSupplicant' in rtw,
 'External key path retained for future Apple RSN': 'cmdInstallExternalKey' in air_c,
}
failed=[name for name,ok in checks.items() if not ok]
for name,ok in checks.items(): print(('PASS' if ok else 'FAIL')+': '+name)
if failed: raise SystemExit('missing contract pieces: '+', '.join(failed))
