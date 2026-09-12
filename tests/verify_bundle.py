#!/usr/bin/env python3
"""Verify bundle metadata and forbidden imports without loading a kext."""
import pathlib, plistlib, subprocess, sys
r=pathlib.Path(__file__).resolve().parents[1]
b=pathlib.Path(sys.argv[1]).resolve() if len(sys.argv)>1 else r/'build/out/AirPort_RTW88.kext'
p=plistlib.loads((b/'Contents/Info.plist').read_bytes())
template=plistlib.loads((r/'AirPort_RTW88.kext/Contents/Info.plist').read_bytes())
assert p==template, 'Packaged plist differs from source'
assert p['CFBundleIdentifier']=='com.rtw88.airport'
assert p['CFBundleVersion']==p['CFBundleShortVersionString']=='1.0.1'
assert p['CFBundlePackageType']=='KEXT'
assert 'com.apple.kpi.private' not in p['OSBundleLibraries']
exe=b/'Contents/MacOS'/p['CFBundleExecutable'];assert exe.is_file()
assert b'com.rtw88.airport\x00' in exe.read_bytes() and b'1.0.1\x00' in exe.read_bytes()
for personality in p['IOKitPersonalities'].values():
    assert personality['CFBundleIdentifier']==p['CFBundleIdentifier']
    assert personality['IOProviderClass']=='IOPCIDevice'
    assert personality['IOPCIMatch']=='0xB82210EC'
undefined=subprocess.check_output(['xcrun','nm','-uj',str(exe)],text=True).splitlines()
for symbol in ['_thread_call_cancel_wait','_OSRuntimeInitializeCPP','_OSRuntimeFinalizeCPP']:
    assert symbol not in undefined, symbol
subprocess.run(['codesign','--verify','--strict',str(b)],check=True)
print('PASS: bundle/template versions, executable, PCI personality, ad hoc signature and forbidden imports')
