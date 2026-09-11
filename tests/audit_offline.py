#!/usr/bin/env python3
"""Offline regression checks: no kext loading, EFI writes or root required."""
import pathlib, re, shlex, subprocess, tempfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
def run(args, **kw):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kw)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {args}\n{result.stdout}\n{result.stderr}")
    return result.stdout
commands = run(['make', '-Bn', 'airport'], cwd=ROOT).splitlines()
def compiler(source, language, output):
    needle = '/main.c' if language == 'c' else '/AirportRTW88.cpp'
    line = next(x for x in commands if x.startswith('xcrun clang') and ' -c ' in x and needle in x)
    args=shlex.split(line)
    ci=args.index('-c'); oi=args.index('-o')
    args[ci:ci+2]=['-S', str(source)]
    args[oi+1]=str(output)
    run(args, cwd=ROOT)
    asm=output.read_text()
    part=asm.split('_rtw_audit_layout:')[1].split('.subsections_via_symbols')[0]
    return [int(x,0) for x in re.findall(r'\.quad\s+(\d+)',part)]
with tempfile.TemporaryDirectory(prefix='rtw88-audit-') as td:
    tmp=pathlib.Path(td)
    types=['ieee80211_hw','ieee80211_vif','ieee80211_sta','ieee80211_ops','ieee80211_bss_conf','sk_buff','pci_dev','wiphy','work_struct','delayed_work','timer_list','ieee80211_tx_info','ieee80211_rx_status','ieee80211_mgmt','ieee80211_hdr','ieee80211_hdr_3addr','ieee80211_key_conf','ieee80211_tx_queue_params']
    fields={'ieee80211_hw':['priv','wiphy','ops','vif_data_size','sta_data_size','conf','kext_hw'], 'ieee80211_vif':['drv_priv','bss_conf','addr'], 'ieee80211_sta':['drv_priv'], 'ieee80211_ops':['start','stop','add_interface','remove_interface','config','tx','set_key'], 'sk_buff':['data','len']}
    expressions=['sizeof(struct '+x+')' for x in types]
    expressions += ['__builtin_offsetof(struct '+t+','+f+')' for t,fs in fields.items() for f in fs]
    body='unsigned long rtw_audit_layout[] = {'+','.join(expressions)+'};\n'
    c=tmp/'layout.c'; c.write_text('#include "rtw88_compat.h"\n'+body)
    cpp=tmp/'layout.cpp';cpp.write_text('#include "RTW88IEEE80211.hpp"\nextern "C" {\n#include "rtw88_compat.h"\n'+body+'}\n')
    cv=compiler(c,'c',tmp/'c.s');cpv=compiler(cpp,'c++',tmp/'cpp.s')
    assert len(cv)==len(expressions),(len(cv),len(expressions))
    differences=[(e,a,b) for e,a,b in zip(expressions,cv,cpv) if a!=b]
    assert not differences, differences
    print('PASS: C/C++ ABI agrees for',len(expressions),'sizes/offsets')
    c.write_text('#include "rtw88_compat.h"\n#include "main.h"\nunsigned long rtw_audit_layout[]={sizeof(struct rtw_vif)};\n')
    private_size=compiler(c,'c',tmp/'vif.s')[0]
    print('rtw_vif private bytes required:',private_size,'(old fixed allocation: 128)')
    # Compile real entry-point source against minimal userspace types. Any
    # reintroduced OSRuntime calls fail the link because no such stubs exist.
    mach=tmp/'mach';mach.mkdir()
    (mach/'mach_types.h').write_text('typedef int kern_return_t;\n#define KERN_SUCCESS 0\n')
    (mach/'kmod.h').write_text('typedef struct {const char *name; const char *version;} kmod_info_t;\ntypedef int kmod_start_func_t(kmod_info_t *, void *);\ntypedef int kmod_stop_func_t(kmod_info_t *, void *);\n#define KMOD_EXPLICIT_DECL(a,b,c,d)\n')
    test=tmp/'entry_test.c'
    test.write_text('#include "'+str(ROOT/'src/kext/kmod_info.c')+'"\nint main(void) { kmod_info_t k={"com.rtw88.airport","test"}; return _realmain(&k,0) || _antimain(&k,0); }\n')
    exe=tmp/'entry_test'; run(['xcrun','clang','-I'+str(tmp),'-DRTW88_AIRPORT_KMOD=1',str(test),'-o',str(exe)])
    print(run([str(exe)]).strip());print('PASS: actual module callbacks execute without runtime reinitialization')

# Exercise the real completion timeout helper with deterministic time and a
# lock mock that reports locks left held, double-locks and double-unlocks.
with tempfile.TemporaryDirectory(prefix='rtw88-completion-') as td:
    tmp=pathlib.Path(td)
    src=tmp/'completion_test.c'
    src.write_text(r'''#include "linux/completion.h"
struct IOLock { int held; };
static struct IOLock test_lock;
static int errors;
static uint64_t ticks;
static struct completion *finish_on_sleep;
IOLock *IOLockAlloc(void) { return &test_lock; }
void IOLockLock(IOLock *l) { if (l->held) errors++; l->held=1; }
void IOLockUnlock(IOLock *l) { if (!l->held) errors++; l->held=0; }
void IOLockWakeup(IOLock *l,void *e,int one) { (void)l;(void)e;(void)one; }
uint64_t mach_absolute_time(void) { return ticks; }
void absolutetime_to_nanoseconds(uint64_t a,uint64_t *n) { *n=a; }
void IOSleep(unsigned ms) { ticks += (uint64_t)ms*1000000; if(finish_on_sleep) { complete(finish_on_sleep); finish_on_sleep=0; } }
int main(void) {
    struct completion c; init_completion(&c);
    if(wait_for_completion_timeout(&c,0)!=0 || test_lock.held) return 1;
    if(wait_for_completion_timeout(&c,3)!=0 || test_lock.held) return 2;
    complete(&c);
    if(wait_for_completion_timeout(&c,3)!=1 || c.done || test_lock.held) return 3;
    finish_on_sleep=&c;
    if(wait_for_completion_timeout(&c,3)!=1 || c.done || test_lock.held) return 4;
    complete_all(&c);
    if(wait_for_completion_timeout(&c,0)!=1 || c.done!=~0u || test_lock.held) return 5;
    return errors?6:0;
}
''')
    line=next(x for x in commands if x.startswith('xcrun clang ') and ' -c ' in x and '/main.c' in x)
    args=shlex.split(line); args[args.index('-c')+1]=str(src); obj=tmp/'completion.o'; args[args.index('-o')+1]=str(obj)
    run(args,cwd=ROOT)
    exe=tmp/'completion_test';run(['xcrun','clang',str(obj),'-o',str(exe)]);run([str(exe)])
    print('PASS: completion immediate/delayed timeout, completion and complete_all; locks balanced')

# Compile the actual workqueue section with the actual compatibility header,
# then run concurrent cancellation/drain cases using pthread-backed XNU mocks.
with tempfile.TemporaryDirectory(prefix='rtw88-workqueue-') as td:
    tmp=pathlib.Path(td)
    impl=(ROOT/'src/compat/rtw88_compat.c').read_text()
    start=impl.index('/* One state lock')
    end=impl.index('/*  Timer implementation',start)
    section=impl[start:end].rsplit('/* ------------------------------------------------------------------ */',1)[0]
    src=tmp/'workqueue_test.c'
    src.write_text('#include "rtw88_compat.h"\n'+section+(ROOT/'tests/workqueue_cases.c').read_text())
    line=next(x for x in commands if x.startswith('xcrun clang ') and ' -c ' in x and '/main.c' in x)
    args=shlex.split(line);args[args.index('-c')+1]=str(src);obj=tmp/'workqueue.o';args[args.index('-o')+1]=str(obj)
    run(args,cwd=ROOT)
    exe=tmp/'workqueue_test';run(['xcrun','clang',str(obj),str(ROOT/'tests/workqueue_runtime.c'),'-pthread','-o',str(exe)])
    for _ in range(10):run([str(exe)],timeout=10)
    print('PASS: workqueue cancellation unlinks queued work, waits for execution; flush waits; delayed work preserves target queue (10 runs)')

with tempfile.TemporaryDirectory(prefix='rtw88-thread-call-') as td:
    tmp=pathlib.Path(td);src=tmp/'thread_call_test.c'
    src.write_text((ROOT/'src/compat/rtw88_thread_call.c').read_text()+'\n'+(ROOT/'tests/thread_call_cases.c').read_text())
    line=next(x for x in commands if x.startswith('xcrun clang ') and ' -c ' in x and '/main.c' in x)
    args=shlex.split(line);args[args.index('-c')+1]=str(src);obj=tmp/'call.o';args[args.index('-o')+1]=str(obj)
    run(args,cwd=ROOT)
    exe=tmp/'call_test';run(['xcrun','clang',str(obj),str(ROOT/'tests/workqueue_runtime.c'),'-pthread','-o',str(exe)])
    for _ in range(10):run([str(exe)],timeout=10)
    print('PASS: public-KPI thread-call wrapper cancels queued work, waits for pre-dispatch/running callbacks, safely frees handles (10 runs)')
