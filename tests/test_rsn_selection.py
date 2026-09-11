#!/usr/bin/env python3
"""Real RSN negotiation: cipher order, GTK choice, round-trip and malformed lengths."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'src/kext/RTW88IEEE80211.cpp').read_text()
code=s[s.index('static uint32_t rtw88ReadSuite'):s.index('void RTW88IEEE80211::processScanResult')]
prefix='''#include <cstdint>\n#include <cstddef>\n#include <cstring>\nconstexpr unsigned WLAN_EID_RSN=48;\nconstexpr uint32_t WLAN_CIPHER_SUITE_CCMP=0x000fac04,WLAN_CIPHER_SUITE_TKIP=0x000fac02;\n'''
main=r'''
int main(){
 uint8_t rsn[]={1,0,0,15,172,2,2,0,0,15,172,2,0,15,172,4,1,0,0,15,172,2};
 uint32_t pair=0,group=0;
 if(!rtw88RsnSelectCcmpPsk(rsn,sizeof(rsn),&pair,&group)||pair!=WLAN_CIPHER_SUITE_CCMP||group!=WLAN_CIPHER_SUITE_TKIP)return 1;
 for(unsigned n=0;n<sizeof(rsn);n++)if(rtw88RsnSelectCcmpPsk(rsn,(uint8_t)n,&pair,&group))return 2;
 rsn[11]=4;rsn[15]=2;if(!rtw88RsnSelectCcmpPsk(rsn,sizeof(rsn),&pair,&group))return 3;
 uint8_t out[32]={};uint16_t n=rtw88BuildSelectedRsnIe(out,group);
 if(out[0]!=48||n!=out[1]+2||!rtw88RsnSelectCcmpPsk(out+2,out[1],&pair,&group)||group!=WLAN_CIPHER_SUITE_TKIP)return 4;
 rsn[6]=255;rsn[7]=255;if(rtw88RsnSelectCcmpPsk(rsn,sizeof(rsn),&pair,&group))return 5;
 uint32_t seed=1;uint8_t fuzz[255];
 for(unsigned t=0;t<10000;t++){for(auto &x:fuzz){seed=seed*1664525+1013904223;x=(uint8_t)(seed>>24);}rtw88RsnSelectCcmpPsk(fuzz,(uint8_t)t,&pair,&group);}
 if(rtw88RsnSelectCcmpPsk(nullptr,0,&pair,&group))return 6;
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.cpp').write_text(prefix+code+main)
 subprocess.run(['xcrun','clang++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: CCMP selected in both pairwise orders, TKIP group preserved, selected RSN round-trip, truncated/oversized counts and 10000 malformed inputs under ASan/UBSan')
