from pathlib import Path
root=Path(__file__).resolve().parents[1]
main=(root/'src/kext/AirportRTW88.cpp').read_text()
awdl=(root/'src/kext/AirportRTW88AWDL.cpp').read_text()
assert 'capabilities[2] = 0xFF' in main
assert 'capabilities[3] = 0x2B' in main
assert 'capabilities[5] = 0x40' in main
assert 'capabilities[6] = 0x8C' in main
assert 'capabilities[8] = 0x0201' in main or 'capabilities[8] = 0x201' in main or 'capabilities[8] = 0x0201' in main or '*(uint16_t *)&d->capabilities[8] = 0x0201' in main
assert 'capabilities[7] = 0x84' not in main
assert 'sizeof(apple80211_scan_result) == 1164' in main
assert 'SInt32 ret = super::disableVirtualInterface(interface)' in awdl
print('AirportItlwm RC contract: OK')
