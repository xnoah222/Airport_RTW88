#!/usr/bin/env python3
"""Execute real medium setup with fault injection into its IOKit calls."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'src/kext/AirportRTW88.cpp').read_text()
s=s[s.index('bool AirportRTW88::configureInterface('):s.index('UInt32 AirportRTW88::outputPacket(')]
code='''
using IOReturn=int;constexpr int kIOReturnSuccess=0,kIOReturnUnsupported=1,kIOReturnError=2,kIOMediumIEEE80211Auto=128;
int fail=0,refs=0,order=0;
void IOLog(const char*,...){}
struct IONetworkInterface{};
struct OSDictionary{static OSDictionary *withCapacity(int){if(fail==1)return nullptr;refs++;return new OSDictionary;}void release(){refs--;delete this;}};
struct IONetworkMedium{unsigned getType()const{return 128;}static IONetworkMedium *medium(int,int){if(fail==2)return nullptr;refs++;return new IONetworkMedium;}static bool addMedium(OSDictionary*,IONetworkMedium*){return fail!=3;}void release(){refs--;delete this;}};
struct Base{bool configureInterface(IONetworkInterface*){return fail!=9;}};
struct AirportRTW88:Base{
 bool publishMediumDictionary(OSDictionary*){order=1;return fail!=4;}
 bool setCurrentMedium(const IONetworkMedium*){if(order!=1)return false;order=2;return fail!=5;}
 bool setSelectedMedium(const IONetworkMedium*){if(order!=2)return false;order=3;return fail!=6;}
 bool configureInterface(IONetworkInterface*);int selectMedium(const IONetworkMedium*);
};
#define super Base
'''+s+'''
int main(){AirportRTW88 d;IONetworkInterface i;
 if(!d.configureInterface(&i)||order!=3||refs)return 1;
 for(int f=1;f<=6;f++){fail=f;order=0;if(d.configureInterface(&i)||refs)return 2;}
 fail=9;if(d.configureInterface(&i)||refs)return 3;
 if(d.selectMedium(nullptr)!=kIOReturnUnsupported)return 4;
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.cpp').write_text(code)
 subprocess.run(['xcrun','clang++','-std=c++11',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: actual medium setup publishes, sets current then selected; allocation/publication/selection failures release temporary references')
