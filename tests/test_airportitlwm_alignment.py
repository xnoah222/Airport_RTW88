from pathlib import Path
root = Path(__file__).resolve().parents[1]
air_h = (root/'src/kext/AirportRTW88.hpp').read_text()
air_c = (root/'src/kext/AirportRTW88.cpp').read_text()
iface = (root/'src/kext/AirportRTW88Interface.cpp').read_text()
rtw = (root/'src/kext/RTW88IEEE80211.cpp').read_text()
checks = {
    'stable internal supplicant mode': 'useAppleRSNSupplicant(IO80211Interface *) override { return false; }' in air_h,
    'Ventura EAPOL IO80211 path': 'ethertype == 0x888E' in iface and 'IO80211Interface::inputPacket' in iface,
    'SSID set benign': 'if (set) return kIOReturnSuccess; // AirportItlwm accepts this' in air_c,
    'BSSID set benign': 'if (isSet) return kIOReturnSuccess;' in air_c,
    'scan multiple accepted': 'case APPLE80211_IOC_SCAN_REQ_MULTIPLE:' in air_c,
    'scan cache clear accepted': 'case APPLE80211_IOC_SCANCACHE_CLEAR:' in air_c,
    'Ventura scan flags simplified': 'APPLE80211_C_FLAG_ACTIVE |\n                           APPLE80211_C_FLAG_20MHZ' in air_c,
    'scan exposes RSN TLV': 'if (b.ies[pos] == 48) { /* RSN TLV */' in air_c,
    'association derives RSN length': 'd->ad_rsn_ie[1] + 2' in air_c,
    'association uses PMK internally': 'cmdConnect(ssid, nullptr, pmk, bssid, false)' in air_c,
    'disconnected SSID returns ENXIO': 'if (st.state != RTW88_STATE_CONNECTED) return 6;' in air_c,
    'country code implemented': 'case APPLE80211_IOC_COUNTRY_CODE:' in air_c,
    'locale implemented': 'case APPLE80211_IOC_LOCALE:' in air_c,
    'internal EAPOL still present': 'RTW88_STATE_HANDSHAKING && !_externalSupplicant' in rtw,
}
failed=[k for k,v in checks.items() if not v]
for k,v in checks.items(): print(('PASS' if v else 'FAIL')+': '+k)
if failed: raise SystemExit('alignment checks failed: '+', '.join(failed))
