#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
ctl = (root/'src/kext/AirportRTW88AWDL.cpp').read_text()
sta = (root/'src/kext/RTW88IEEE80211.cpp').read_text()
parent = (root/'src/kext/AirportRTW88.cpp').read_text()
compat = (root/'src/compat/rtw88_compat.c').read_text()

checks = {
    'proactive AWDL VIF attach': 'ensureAWDLVirtualInterface()' in (root/'src/kext/AirportRTW88.cpp').read_text(),
    'AWDL output queue drain': 'dequeueOutputPacketsWithServiceClass' in ctl,
    'AWDL native data TX': 'txAWDLDataFrame' in ctl and 'RTW88IEEE80211::txAWDLDataFrame' in sta,
    'direct 802.11 data framing': 'IEEE80211_FTYPE_DATA' in sta,
    'AWDL fixed BSSID': '0x00,0x25,0x00,0xff,0x94,0x73' in sta.lower().replace(' ', ''),
    'AWDL SNAP OUI': 'snap[3] = 0x00; snap[4] = 0x17; snap[5] = 0xf2;' in sta.lower(),
    'AWDL data magic': '0x0403' in sta,
    'AWDL RX classifier': 'tryDeliverAWDLDataFrame' in sta,
    'AWDL RX to virtual interface': 'injectRxAWDLFrame' in sta and 'awdl->inputPacket' in parent,
    'AWDL accept-other-address RX': 'FIF_OTHER_BSS' in sta,
    'AWDL dedicated channel switch': 'rtw88_awdl_switch_channel' in sta and 'rtw88_awdl_switch_channel' in compat,
    'AWDL post-scan channel restore': 'AWDL channel restore after scan' in sta,
    'no optimistic unknown AWDL SET': 'generic opaque' not in ctl.lower(),
}
failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(('PASS: ' if ok else 'FAIL: ') + name)
if failed:
    raise SystemExit('AWDL datapath contract failed: ' + ', '.join(failed))
print('AirPort_RTW88 1.0.1 AWDL datapath contract: OK')
