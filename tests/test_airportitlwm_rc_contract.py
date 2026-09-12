from pathlib import Path
root=Path(__file__).resolve().parents[1]
main=(root/'src/kext/AirportRTW88.cpp').read_text()
awdl=(root/'src/kext/AirportRTW88AWDL.cpp').read_text()
sdk=(root/'MacKernelSDK/Headers/IOKit/80211/apple80211_var.h').read_text()
for token in [
    'capabilities[2] = 0xFF',
    'capabilities[3] = 0x2B',
    'capabilities[4] = 0xAD',
    'capabilities[5] = 0x8C',
    'capabilities[6] = 0x8C',
    'capabilities[7] = 0x84',
]:
    assert token in main, token
assert 'sizeof(apple80211_scan_result) == 1164' in main
assert 'uint8_t               asr_ie_data[1024]' in sdk
assert 'SInt32 ret = super::disableVirtualInterface(interface)' in awdl
print('AirportItlwm RC contract: OK')
