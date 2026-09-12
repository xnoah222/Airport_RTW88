from pathlib import Path
p=Path(__file__).parents[1]/'src/kext/AirportRTW88.cpp'
s=p.read_text()
assert 'SCAN_REQ coalesced with active scan' in s
assert 'SCAN_REQ_MULTIPLE coalesced with active scan' in s
# Neither scan request path should expose EBUSY merely because an existing scan is active.
req=s[s.index('IOReturn AirportRTW88::handleSCAN_REQ'):s.index('void AirportRTW88::fillScanResultFromBSS')]
assert 'if (_scanInProgress) return kIOReturnBusy;' not in req
multi=s[s.index('case APPLE80211_IOC_SCAN_REQ_MULTIPLE:'):s.index('case APPLE80211_IOC_SCANCACHE_CLEAR:')]
assert 'if (_scanInProgress) return kIOReturnBusy;' not in multi
assert 'if (_scanInProgress)' in req and 'return kIOReturnSuccess;' in req
assert 'if (_scanInProgress)' in multi and 'return kIOReturnSuccess;' in multi
print('scan coalescing contract: PASS')
