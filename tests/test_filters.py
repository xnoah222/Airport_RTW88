#!/usr/bin/env python3
"""Execute actual multicast handlers, including rtw88 callback and restart."""
import pathlib,subprocess,tempfile
r=pathlib.Path(__file__).resolve().parents[1]
s=(r/'src/kext/AirportRTW88.cpp').read_text();s=s[s.index('IOReturn AirportRTW88::getPacketFilters('):s.index('IONetworkInterface *AirportRTW88::createInterface(')]
b=(r/'src/kext/RTW88IEEE80211.cpp').read_text();b=b[b.index('IOReturn RTW88IEEE80211::setReceiveMulticast('):b.index('void RTW88IEEE80211::powerOff(')]
code='''
using IOReturn=int;using UInt32=unsigned;
constexpr int kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnUnsupported=2,kIOReturnNotReady=3,kIOReturnError=4;
constexpr unsigned FIF_ALLMULTI=2;
constexpr unsigned kIOPacketFilterUnicast=1,kIOPacketFilterBroadcast=2,kIOPacketFilterMulticast=4;
struct OSSymbol { bool isEqualTo(const OSSymbol *p)const{return this==p;} }; OSSymbol group,other;OSSymbol *gIONetworkFilterGroup=&group;
struct IOEthernetAddress {unsigned char bytes[6];};
void IOLog(const char*,...){}
struct HW;struct Ops{int(*start)(HW*);void(*configure_filter)(HW*,unsigned,unsigned*,unsigned long long);};struct HW{Ops *ops;};
int calls=0,starts=0,startResult=0;unsigned last=0,changed=0;
int start(HW*){starts++;return startResult;}
void filter(HW*,unsigned c,unsigned *f,unsigned long long){calls++;changed=c;last=*f;}
struct RTW88IEEE80211 {HW *_hw;bool _powered=false,_receiveMulticast=true;int powerOn();int setReceiveMulticast(bool);};
struct AirportRTW88 {RTW88IEEE80211 *_ieee80211;int getPacketFilters(const OSSymbol*,unsigned*)const;int setMulticastMode(bool);int setMulticastList(IOEthernetAddress*,unsigned);int setPromiscuousMode(bool);};
'''+b+s+'''
int main(){
 Ops ops={start,filter};HW hw={&ops};RTW88IEEE80211 backend;backend._hw=&hw;AirportRTW88 d;d._ieee80211=&backend;unsigned mask=99;
 if(d.getPacketFilters(&group,&mask)||mask!=7)return 1;
 if(d.getPacketFilters(&other,&mask)||mask)return 2;
 if(d.getPacketFilters(nullptr,&mask)!=kIOReturnBadArgument)return 3;
 if(d.setMulticastMode(false)||calls||backend._receiveMulticast)return 4;
 if(backend.powerOn()||starts!=1||calls!=1||last||changed!=FIF_ALLMULTI)return 5;
 IOEthernetAddress a={};if(d.setMulticastList(&a,1)||last!=FIF_ALLMULTI||calls!=2)return 6;
 if(d.setMulticastList(nullptr,0)||last||calls!=3)return 7;
 if(d.setMulticastList(nullptr,1)!=kIOReturnBadArgument||calls!=3)return 8;
 if(d.setPromiscuousMode(true)!=kIOReturnUnsupported||d.setPromiscuousMode(false))return 9;
 backend._powered=false;startResult=1;if(backend.powerOn()!=kIOReturnError||backend._powered||calls!=3)return 10;
 backend._hw=nullptr;if(d.setMulticastMode(true)!=kIOReturnNotReady)return 11;
 d._ieee80211=nullptr;if(d.setMulticastMode(true)!=kIOReturnNotReady)return 12;
}
'''
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td);(p/'test.cpp').write_text(code)
 subprocess.run(['xcrun','clang++','-std=c++11',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: actual filter handlers, hardware callback, deferred state/restart, advertised mask and errors')
