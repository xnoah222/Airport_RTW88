#!/usr/bin/env python3
"""Execute actual interface lifecycle overrides with a recording superclass."""
import pathlib,subprocess,tempfile
r=pathlib.Path(__file__).resolve().parents[1]
s=(r/'src/kext/AirportRTW88.cpp').read_text()
s=s[s.index('IOReturn AirportRTW88::enable('):s.index('IOReturn AirportRTW88::getPacketFilters(')]
code='''
using IOReturn=int;
constexpr int kIOReturnNotReady=123,kIOReturnSuccess=0;
struct Backend { int result=0,on=0,off=0; int powerOn(){on++;return result;} void powerOff(){off++;} };
struct IONetworkInterface {};
void IOLog(const char*,...) {}
struct Base {
 int enables=0,disables=0,result=0; IONetworkInterface *seen=nullptr;
 int enable(IONetworkInterface *p){enables++;seen=p;return result;}
 int disable(IONetworkInterface *p){disables++;seen=p;return result;}
};
struct AirportRTW88:Base {
 Backend *_ieee80211=nullptr;
 int enable(IONetworkInterface*);int disable(IONetworkInterface*);
};
#define super Base
'''+s+'''
int main(){
 AirportRTW88 d;IONetworkInterface i;
 if(d.enable(&i)!=kIOReturnNotReady||d.enables)return 1;
 Backend backend;d._ieee80211=&backend;
 if(d.enable(&i)||d.enables!=1||d.seen!=&i)return 2;
 d.result=42;
 if(d.enable(&i)!=42||d.enables!=2||backend.off!=1)return 3;
 backend.result=88;if(d.enable(&i)!=88||d.enables!=2)return 6;
 if(d.disable(&i)!=42||d.disables!=1||d.seen!=&i)return 4;
 d.result=0;if(d.disable(&i)||d.disables!=2)return 5;
}
'''
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td);(p/'test.cpp').write_text(code)
 subprocess.run(['xcrun','clang++','-std=c++11',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: real enable/disable overrides delegate once, preserve interface and errors, reject absent backend')
