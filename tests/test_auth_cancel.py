#!/usr/bin/env python3
"""Exercise real authentication/cancel functions with deterministic cancellation points."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'src/kext/RTW88IEEE80211.cpp').read_text()
a=s[s.index('void RTW88IEEE80211::doAuthenticate()'):s.index('void RTW88IEEE80211::doAssociate()')]
assoc=s[s.index('void RTW88IEEE80211::doAssociate()'):s.index('void RTW88IEEE80211::processAssocResponse(')]
c=s[s.index('void RTW88IEEE80211::cancelAuthentication()'):s.index('IOReturn RTW88IEEE80211::cmdDisconnect()')]
prefix=r'''
#include <cstdint>
#include <cstring>
using UInt32=uint32_t;
constexpr int NL80211_NUM_BANDS=1,kMillisecondScale=1;
enum State {RTW88_STATE_IDLE,RTW88_STATE_AUTHENTICATING,RTW88_STATE_ASSOCIATING,RTW88_STATE_CONNECTED};
struct ieee80211_channel {int hw_value=6,band=0;};
struct ieee80211_supported_band {int n_channels=1,band=0;ieee80211_channel channels[1];};
struct Wiphy {ieee80211_supported_band* bands[1];};struct Hw {Wiphy*wiphy;};
struct ieee80211_bss_conf {uint8_t*bssid;uint8_t bssid_buf[6];bool assoc;int aid;};
struct Vif {ieee80211_bss_conf bss_conf;};
struct Timer {int wakes=0,cancels=0;void wakeAtTime(uint64_t){wakes++;}void cancelTimeout(){cancels++;}};
void IOLog(const char*,...){};
void clock_interval_to_deadline(int,int,uint64_t*out){*out=1;}
int scanning=0,sleepCalls=0,cancelAt=0,drained=0;bool advanceOnTx=false,txFails=false;State nextState=RTW88_STATE_ASSOCIATING;
struct RTW88IEEE80211 {
 bool _connectCancelled=false,_powered=true;Hw*_hw;Vif*_vif;Timer*_timer;void*_connectTC=(void*)1;
 struct {uint8_t bssid[6]={};unsigned channel=6;} _targetBSS;
 State _state=RTW88_STATE_AUTHENTICATING;int sent=0;
 bool authenticationCancelled()const{return __atomic_load_n(&_connectCancelled,__ATOMIC_ACQUIRE);}
 void doAuthenticate();void doAssociate();void cancelAuthentication();void setConnectedChandef(ieee80211_channel*){}
 void buildAuthReq(uint8_t*,uint32_t*n){*n=30;}
 bool buildAssocReq(uint8_t*,uint32_t*n){*n=40;return true;}
 bool txMgmtFrame(uint8_t*,uint32_t){sent++;if(advanceOnTx)_state=nextState;return !txFails;}
};
RTW88IEEE80211*active;
bool rtw88_is_scanning(){return scanning-->0;}
void rtw88_connect_hw_setup(Hw*,Vif*,uint8_t*){}
void IOSleep(int){if(++sleepCalls==cancelAt)active->cancelAuthentication();}
void thread_call_cancel_wait(void*){if(!active->authenticationCancelled()||!active->_timer->cancels)__builtin_trap();drained++;}
'''
main=r'''
int main(){
 ieee80211_supported_band band;Wiphy w{{&band}};Hw hw{&w};Vif vif{};
 for(int test=0;test<5;test++){
  Timer timer;RTW88IEEE80211 obj;obj._hw=&hw;obj._vif=&vif;obj._timer=&timer;active=&obj;
  scanning=test==1?1:0;sleepCalls=0;cancelAt=test==1||test==2?1:0;drained=0;advanceOnTx=test==4;
  if(test==0)obj.cancelAuthentication();
  obj.doAuthenticate();
  if(test<=2&&(obj.sent||timer.wakes||drained!=1))return 1+test;
  if(test==3&&(obj.sent!=1||timer.wakes!=1))return 4;
  if(test==4&&(obj.sent!=1||timer.wakes||obj._state!=RTW88_STATE_ASSOCIATING))return 5;
 }
 for(int test=0;test<3;test++){
  Timer timer;RTW88IEEE80211 obj;obj._hw=&hw;obj._vif=&vif;obj._timer=&timer;active=&obj;
  advanceOnTx=test==0;nextState=RTW88_STATE_CONNECTED;txFails=test==1;
  if(test==2)obj.cancelAuthentication();
  obj.doAssociate();
  if(timer.wakes)return 6;
  if(test==0&&obj._state!=RTW88_STATE_CONNECTED)return 7;
  if(test==1&&obj._state!=RTW88_STATE_IDLE)return 8;
  if(test==2&&obj.sent)return 9;
 }
}
''' 
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.cpp').write_text(prefix+a+assoc+c+main)
 subprocess.run(['xcrun','clang++','-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: real authentication cancels before work/during scan/during settle; drains call; no post-cancel TX/timer; RX state preserved')
