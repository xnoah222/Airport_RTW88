from pathlib import Path
root=Path(__file__).resolve().parents[1]
h=(root/'src/kext/AirportRTW88.hpp').read_text()
c=(root/'src/kext/AirportRTW88AWDL.cpp').read_text()
b=(root/'src/kext/RTW88IEEE80211.cpp').read_text()
mk=(root/'Makefile').read_text()
plist=(root/'AirPort_RTW88.kext/Contents/Info.plist').read_text()
assert 'createVirtualInterface(ether_addr *addr, UInt role)' in h
assert 'apple80211VirtualRequest' in h
assert 'APPLE80211_VIF_AWDL' in c and '"awdl"' in c
assert 'enableVirtualInterface' in c and 'setEnabledBySystem(true)' in c
assert 'APPLE80211_IOC_AWDL_SYNC_ENABLED' in c
assert 'APPLE80211_IOC_AWDL_SYNC_FRAME_TEMPLATE' in c
assert 'txRawManagementFrame' in b
assert 'injectRxActionFrame' in b
assert 'AirPort_RTW88.kext' in mk and 'AirPort_RTW88' in mk
assert '<string>1.0.1</string>' in plist
assert '<string>AirPort_RTW88</string>' in plist

sta=(root/'src/kext/AirportRTW88.cpp').read_text()
assert 'case APPLE80211_IOC_VIRTUAL_IF_CREATE:' in sta
assert 'handleVIRTUAL_IF_CREATE' in sta and 'attachVirtualInterface' in sta
assert 'case APPLE80211_IOC_VIRTUAL_IF_DELETE:' in sta
assert 'handleVIRTUAL_IF_DELETE' in sta and 'detachVirtualInterface' in sta
for expected in ['capabilities[2] = 0xFF', 'capabilities[3] = 0x2B',
                 'capabilities[4] = 0xAD', 'capabilities[5] = 0x8C',
                 'capabilities[6] = 0x8C', 'capabilities[7] = 0x84']:
    assert expected in sta, expected
assert 'DLT_IEEE802_11_RADIO' in c and 'mbuf_adj' in c

print('AWDL/VIF static contract: OK')
