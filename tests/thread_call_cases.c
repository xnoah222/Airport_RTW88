extern void mock_fire(void *);
extern void mock_allow_dispatch(void);
static int calls, entered, release_callback, canceled_done;
static void callback(thread_call_param_t a, thread_call_param_t b) { (void)a;(void)b;__atomic_add_fetch(&calls,1,__ATOMIC_SEQ_CST);__atomic_store_n(&entered,1,__ATOMIC_SEQ_CST);while(!__atomic_load_n(&release_callback,__ATOMIC_SEQ_CST))IOSleep(1); }
static int wait_flag(int *p) { for(int i=0;i<2000;i++){if(__atomic_load_n(p,__ATOMIC_SEQ_CST))return 1;IOSleep(1);}return 0; }
static void cancel_thread(void *h,wait_result_t wr) { (void)wr;rtw88_thread_call_cancel_wait(h);__atomic_store_n(&canceled_done,1,__ATOMIC_SEQ_CST);thread_terminate(current_thread()); }
int main(void) {
    thread_call_t h=rtw88_thread_call_allocate(callback,NULL);if(!h)return 1;
    if(rtw88_thread_call_enter(h)||!rtw88_thread_call_enter(h))return 2;
    if(!rtw88_thread_call_cancel_wait(h)||calls)return 3;
    if(rtw88_thread_call_enter_delayed(h,10))return 4;
    struct rtw88_call *c=(struct rtw88_call *)h;
    mock_fire(c->native); /* native dequeued, trampoline has not entered */
    thread_t t;kernel_thread_start(cancel_thread,h,&t);IOSleep(20);
    if(__atomic_load_n(&canceled_done,__ATOMIC_SEQ_CST))return 5;
    mock_allow_dispatch();if(!wait_flag(&canceled_done)||calls)return 6;
    __atomic_store_n(&canceled_done,0,__ATOMIC_SEQ_CST);
    rtw88_thread_call_enter(h);mock_fire(c->native);mock_allow_dispatch();
    if(!wait_flag(&entered))return 7;
    kernel_thread_start(cancel_thread,h,&t);IOSleep(20);
    if(__atomic_load_n(&canceled_done,__ATOMIC_SEQ_CST))return 8;
    __atomic_store_n(&release_callback,1,__ATOMIC_SEQ_CST);
    if(!wait_flag(&canceled_done)||calls!=1)return 9;
    if(!rtw88_thread_call_free(h))return 10;
    return 0;
}
