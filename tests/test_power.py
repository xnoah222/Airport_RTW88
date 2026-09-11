#!/usr/bin/env python3
"""Run the real POWER/STATE handlers against SDK types and a fake backend."""
import pathlib, subprocess, shlex, tempfile
r=pathlib.Path(__file__).resolve().parents[1]
s=(r/'src/kext/AirportRTW88.cpp').read_text()
state=s[s.index('IOReturn AirportRTW88::handleSTATE('):s.index('IOReturn AirportRTW88::handleCHANNEL(')]
power=s[s.index('IOReturn AirportRTW88::handlePOWER('):s.index('SInt32 AirportRTW88::stopDMA(')]
code=r'''
#include "RTW88IEEE80211.hpp"
#include "RTW88UserClient.hpp"
#include <IOKit/80211/apple80211_ioctl.h>
extern "C" void IOLog(const char *,...) {}
struct Backend {
 int on=0,off=0; IOReturn result=kIOReturnSuccess; RTW88StateResult state={};
 IOReturn cmdPowerOn(){on++; if(!result)state.powered=1; return result;}
 IOReturn cmdPowerOff(){off++; if(!result)state.powered=0; return result;}
 IOReturn cmdGetState(RTW88StateResult *out){if(!result)*out=state;return result;}
};
struct Interface { int messages=0; void postMessage(unsigned n){if(n==APPLE80211_M_POWER_CHANGED)messages++;} };
class AirportRTW88 { public:
 Backend *_ieee80211; Interface *_netif;
 IOReturn handlePOWER(bool,apple80211_power_data *);
 IOReturn handleSTATE(apple80211_state_data *);
};
'''+state+power+r'''
extern "C" int main(){
 Backend backend; Interface iface; AirportRTW88 driver;driver._ieee80211=&backend;driver._netif=&iface;
 struct { unsigned before; apple80211_power_data value; unsigned after; } guarded={0x12345678,{},0xabcdef01};
 auto &p=guarded.value;p.version=1;p.num_radios=1;p.power_state[0]=APPLE80211_POWER_OFF;
 if(driver.handlePOWER(true,&p)||backend.off!=1||backend.on||iface.messages!=1)return 1;
 p.power_state[0]=APPLE80211_POWER_ON;
 if(driver.handlePOWER(true,&p)||backend.on!=1||iface.messages!=2)return 2;
 if(driver.handlePOWER(false,&p)||p.version!=1||p.num_radios!=1||p.power_state[0]!=1)return 3;
 for(unsigned i=1;i<APPLE80211_MAX_RADIO;i++)if(p.power_state[i])return 4;
 if(guarded.before!=0x12345678||guarded.after!=0xabcdef01)return 5;
 p.version=9;if(driver.handlePOWER(true,&p)!=kIOReturnBadArgument)return 6;p.version=1;
 p.num_radios=0;if(driver.handlePOWER(true,&p)!=kIOReturnBadArgument)return 7;
 p.num_radios=APPLE80211_MAX_RADIO+1;if(driver.handlePOWER(true,&p)!=kIOReturnBadArgument)return 8;
 p.num_radios=1;p.power_state[0]=APPLE80211_POWER_TX;if(driver.handlePOWER(true,&p)!=kIOReturnUnsupported)return 9;
 p.num_radios=2;p.power_state[0]=1;p.power_state[1]=0;if(driver.handlePOWER(true,&p)!=kIOReturnUnsupported)return 10;
 p.num_radios=1;p.power_state[0]=1;backend.result=kIOReturnError;
 if(driver.handlePOWER(true,&p)!=kIOReturnError||iface.messages!=2)return 11;
 p.version=77;if(driver.handlePOWER(false,&p)!=kIOReturnError||p.version!=77)return 12;
 backend.result=kIOReturnSuccess;
 struct {unsigned before;apple80211_state_data value;unsigned after;} status={1,{},2};
 const unsigned states[]={RTW88_STATE_IDLE,RTW88_STATE_SCANNING,RTW88_STATE_AUTHENTICATING,RTW88_STATE_ASSOCIATING,RTW88_STATE_HANDSHAKING,RTW88_STATE_CONNECTED};
 const unsigned expected[]={APPLE80211_S_INIT,APPLE80211_S_SCAN,APPLE80211_S_AUTH,APPLE80211_S_ASSOC,APPLE80211_S_ASSOC,APPLE80211_S_RUN};
 for(unsigned i=0;i<6;i++){backend.state.state=states[i];if(driver.handleSTATE(&status.value)||status.value.version!=1||status.value.state!=expected[i])return 13;}
 if(status.before!=1||status.after!=2)return 14;
 if(driver.handlePOWER(false,nullptr)!=kIOReturnBadArgument||driver.handleSTATE(nullptr)!=kIOReturnBadArgument)return 15;
 return 0;
}
'''
def run(args):
 p=subprocess.run(args,cwd=r,text=True,capture_output=True)
 if p.returncode:raise RuntimeError(str(args)+'\n'+p.stdout+p.stderr)
 return p.stdout
commands=run(['make','-Bn','airport']).splitlines()
line=next(x for x in commands if x.startswith('xcrun clang++') and ' -c ' in x and '/AirportRTW88.cpp' in x)
with tempfile.TemporaryDirectory(prefix='airport-power-') as td:
 t=pathlib.Path(td);src=t/'power.cpp';src.write_text(code);obj=t/'power.o';exe=t/'power'
 args=shlex.split(line);args[args.index('-c')+1]=str(src);args[args.index('-o')+1]=str(obj);run(args)
 run(['xcrun','clang++',str(obj),'-o',str(exe)]);run([str(exe)])
print('PASS: actual POWER/STATE handlers: ON/OFF independent of version, bounds, invalid requests, backend errors, notifications and state mapping')
