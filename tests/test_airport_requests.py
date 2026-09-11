#!/usr/bin/env python3
"""Portable static checks for the Ventura Apple80211 request surface.

This replaces the old host-compile harness, which had drifted from the actual
backend signatures and only ran on macOS/xcrun.  Real compilation is still
performed by `make airport` on macOS.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/'src/kext/AirportRTW88.cpp').read_text()
required=[
 'APPLE80211_IOC_SSID','APPLE80211_IOC_AUTH_TYPE','APPLE80211_IOC_CHANNEL',
 'APPLE80211_IOC_BSSID','APPLE80211_IOC_SCAN_REQ','APPLE80211_IOC_SCAN_REQ_MULTIPLE',
 'APPLE80211_IOC_SCAN_RESULT','APPLE80211_IOC_CARD_CAPABILITIES','APPLE80211_IOC_STATE',
 'APPLE80211_IOC_PHY_MODE','APPLE80211_IOC_OP_MODE','APPLE80211_IOC_RSSI',
 'APPLE80211_IOC_NOISE','APPLE80211_IOC_INT_MIT','APPLE80211_IOC_POWER',
 'APPLE80211_IOC_ASSOCIATE','APPLE80211_IOC_ASSOCIATE_RESULT','APPLE80211_IOC_DISASSOCIATE',
 'APPLE80211_IOC_SUPPORTED_CHANNELS','APPLE80211_IOC_LOCALE','APPLE80211_IOC_RSN_IE',
 'APPLE80211_IOC_AP_IE_LIST','APPLE80211_IOC_ASSOCIATION_STATUS',
 'APPLE80211_IOC_COUNTRY_CODE','APPLE80211_IOC_RADIO_INFO','APPLE80211_IOC_CIPHER_KEY',
 'APPLE80211_IOC_SCANCACHE_CLEAR'
]
failed=[]
for name in required:
    ok=f'case {name}:' in s
    print(('PASS' if ok else 'FAIL')+': '+name)
    if not ok: failed.append(name)
semantic={
 'scan result exposes RSN TLV only': 'if (b.ies[pos] == 48) { /* RSN TLV */' in s,
 'Ventura scan channels ACTIVE/20MHz': 'APPLE80211_C_FLAG_ACTIVE |\n                           APPLE80211_C_FLAG_20MHZ' in s,
 'SSID GET ENXIO before RUN': 'if (st.state != RTW88_STATE_CONNECTED) return 6;' in s,
 'BSSID SET benign': 'case APPLE80211_IOC_BSSID' in s and 'if (isSet) return kIOReturnSuccess;' in s,
 'country code response exists': '_countryCode' in s,
}
for name,ok in semantic.items():
    print(('PASS' if ok else 'FAIL')+': '+name)
    if not ok: failed.append(name)
if failed: raise SystemExit('Airport request checks failed: '+', '.join(failed))
