from pathlib import Path

src = Path('src/kext/AirportRTW88AWDL.cpp').read_text()
needle = 'case APPLE80211_IOC_COUNTRY_CODE:'
assert needle in src, 'AWDL virtual request path must handle COUNTRY_CODE'
block = src[src.index(needle):src.index(needle)+700]
assert 'apple80211Request(request_type, request_number, _netif, data)' in block, \
    'AWDL COUNTRY_CODE must share the physical controller handler/state'
print('AWDL country-code forwarding: OK')
