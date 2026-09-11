from pathlib import Path
root = Path(__file__).resolve().parents[1]
hpp = (root/'src/kext/AirportRTW88.hpp').read_text()
cpp = (root/'src/kext/AirportRTW88.cpp').read_text()
plist = (root/'AirPort_RTW88.kext/Contents/Info.plist').read_text()
checks = {
    'IO80211 workloop override': 'bool     createWorkLoop() override;' in hpp and 'IO80211WorkLoop::workLoop()' in cpp,
    'controller getWorkLoop override': 'IOWorkLoop *getWorkLoop() const override;' in hpp,
    'no second generic workloop': '_workLoop = IOWorkLoop::workLoop()' not in cpp,
    'AirportItlwm attach=true': 'attachInterface((IONetworkInterface **)&_netif, true)' in cpp,
    'controller service publication': '\n    registerService();\n' in cpp,
    'interface service publication': '_netif->registerService();' in cpp,
    'valid initial link': 'IO80211Controller::setLinkStatus(kIONetworkLinkValid);' in cpp,
    'per-interface MAC query': 'getHardwareAddressForInterface' in hpp and 'AirportRTW88::getHardwareAddressForInterface' in cpp,
    'default match category': '<key>IOMatchCategory</key>' in plist and '<string>IODefaultMatchCategory</string>' in plist,
    'CURRENT_NETWORK dispatcher trace': 'CURRENT_NETWORK dispatcher entry' in cpp,
    'CURRENT_NETWORK state fallback': 'CURRENT_NETWORK reconstructed from connected state' in cpp,
}
for name, ok in checks.items():
    assert ok, name
    print('PASS:', name)
