#!/usr/bin/env python3
"""Compare compiled Ventura request layouts with pinned AirportItlwm headers."""
from pathlib import Path
import re,shlex,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
ref=r/'tests/fixtures/airportitlwm-2.3.0/apple80211_ioctl.h'
types=['apple80211req','apple80211_ssid_data','apple80211_authtype_data','apple80211_channel_data','apple80211_power_data','apple80211_state_data','apple80211_radio_info_data','apple80211_assoc_data','apple80211_scan_data','apple80211_scan_result','apple80211_capability_data','apple80211_phymode_data','apple80211_bssid_data','apple80211_version_data','apple80211_rssi_data']
expr=['sizeof('+t+')' for t in types]+['__builtin_offsetof(apple80211_scan_result,'+f+')' for f in ['asr_ie_data','asr_ie_len','asr_ssid','asr_age']]+['__builtin_offsetof(apple80211_assoc_data,'+f+')' for f in ['ad_key','ad_rsn_ie','ad_flags']]
def run(args):return subprocess.check_output(args,cwd=r,text=True)
line=next(x for x in run(['make','-Bn','airport']).splitlines() if x.startswith('xcrun clang++') and ' -c ' in x and '/AirportRTW88.cpp' in x)
with tempfile.TemporaryDirectory() as td:
 p=Path(td);results=[]
 for i,header in enumerate([r/'MacKernelSDK/Headers/IOKit/80211/apple80211_ioctl.h',ref]):
  src=p/f'{i}.cpp';obj=p/f'{i}.s'
  src.write_text('#include "'+str(header)+'"\nextern "C" { unsigned long abi[] = {'+','.join(expr)+'}; }\n')
  args=shlex.split(line);args[args.index('-c')+1]=str(src);args[args.index('-c')]='-S';args[args.index('-o')+1]=str(obj)
  subprocess.run(args,cwd=r,check=True,stdout=subprocess.DEVNULL)
  results.append([int(x) for x in re.findall(r'\.quad\s+(\d+)',obj.read_text().split('_abi:')[1])])
 for e,a,b in zip(expr,*results):
  if a!=b:print('MISMATCH',e,a,b)
 assert results[0]==results[1], 'Ventura request ABI differs from reference'
 print('PASS:',len(expr),'compiled ABI sizes/offsets match AirportItlwm v2.3.0 for Ventura')
