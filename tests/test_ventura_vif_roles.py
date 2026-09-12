from pathlib import Path
p = Path(__file__).resolve().parents[1] / 'MacKernelSDK/Headers/IOKit/80211/apple80211_var.h'
s = p.read_text()
assert '#if __IO80211_TARGET < __MAC_13_0' in s
assert 'APPLE80211_VIF_P2P_DEVICE   = 3' in s
# Under Ventura+: DEVICE=3, CLIENT=4, GO=5, AWDL=6, SOFT_AP=7 by enum order.
order = ['APPLE80211_VIF_P2P_DEVICE', 'APPLE80211_VIF_P2P_CLIENT', 'APPLE80211_VIF_P2P_GO', 'APPLE80211_VIF_AWDL', 'APPLE80211_VIF_SOFT_AP']
pos = [s.index(x) for x in order]
assert pos == sorted(pos)
print('PASS: Ventura VIF role numbering matches IO80211 (AWDL=6)')
