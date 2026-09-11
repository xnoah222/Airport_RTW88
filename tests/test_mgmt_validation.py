#!/usr/bin/env python3
"""Exercise the production frame parser with valid, truncated and wrong-peer frames."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
code=r'''
#include "RTW88MgmtValidation.hpp"
#include <cstring>
int main(){
 uint8_t mac[6]={2,3,4,5,6,7},ap[6]={8,9,10,11,12,13},f[30]={};
 memcpy(f+4,mac,6);memcpy(f+10,ap,6);memcpy(f+16,ap,6);
 uint16_t status=99,aid=88;
 f[0]=0xb0;f[26]=2;
 for(unsigned n=0;n<30;n++)if(rtw88AuthResponse(f,n,mac,ap,&status)||status!=99)return 1;
 if(!rtw88AuthResponse(f,30,mac,ap,&status)||status!=0)return 2;
 f[26]=1;if(rtw88AuthResponse(f,30,mac,ap,&status))return 3;f[26]=2;
 f[24]=1;if(rtw88AuthResponse(f,30,mac,ap,&status))return 4;f[24]=0;
 for(unsigned off: {4U,10U,16U}){f[off]^=1;if(rtw88AuthResponse(f,30,mac,ap,&status))return 5;f[off]^=1;}
 f[0]=0x10;f[26]=0;f[28]=42;f[29]=0xc0;
 for(unsigned n=0;n<30;n++)if(rtw88AssocResponse(f,n,mac,ap,&status,&aid))return 6;
 if(!rtw88AssocResponse(f,30,mac,ap,&status,&aid)||status||aid!=42)return 7;
 // Fields remain valid after the caller releases/poisons the packet storage.
 memset(f,0xdd,30);if(status||aid!=42)return 8;
 memcpy(f+4,mac,6);memcpy(f+10,ap,6);memcpy(f+16,ap,6);f[0]=0x30;f[26]=0;f[27]=0;f[28]=0;f[29]=0;
 if(rtw88AssocResponse(f,30,mac,ap,&status,&aid))return 9;
 f[26]=17;if(!rtw88AssocResponse(f,30,mac,ap,&status,&aid)||status!=17)return 10;
 return 0;
}
'''.replace('#include <cstring>','#include <cstring>\n#include <initializer_list>')
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.cpp').write_text(code)
 subprocess.run(['xcrun','clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(r/'src/kext'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: response lengths 0..29, peer/algorithm/sequence validation, rejection status, AID and copied-field lifetime (ASan/UBSan)')
