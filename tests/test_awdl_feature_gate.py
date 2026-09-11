from pathlib import Path
p = Path(__file__).resolve().parents[1] / 'src/kext/AirportRTW88.cpp'
s = p.read_text()
block = s[s.index('SInt32 AirportRTW88::enableFeature'):s.index('SInt32 AirportRTW88::monitorModeSetEnabled')]
assert 'if (feature == kIO80211Feature80211n)' in block
assert 'return kIOReturnSuccess;' in block
assert 'return 102;' in block
assert 'enableFeature code=%u' not in block
print('AWDL feature gate matches AirportItlwm: OK')
