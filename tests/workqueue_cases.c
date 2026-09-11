/* Included after the real workqueue implementation in the test translation unit. */
/* Deterministic timer substitutes: tests explicitly deliver timer callbacks. */
void timer_setup(struct timer_list *t,void (*fn)(struct timer_list *),unsigned flags) { (void)flags;bzero(t,sizeof(*t));t->function=fn; }
int mod_timer(struct timer_list *t,unsigned long expires) { t->expires=expires;t->active=1;t->call=(thread_call_t)t;return 0; }
int del_timer(struct timer_list *t) { int old=t->active;t->active=0;return old; }
int del_timer_sync(struct timer_list *t) { int old=del_timer(t);t->call=NULL;return old; }
static int entered, release_callback, calls, cancel_done, flush_done;
static struct work_struct blocked;
static int read_flag(int *p) { return __atomic_load_n(p,__ATOMIC_SEQ_CST); }
static void flag(int *p,int v) { __atomic_store_n(p,v,__ATOMIC_SEQ_CST); }
static void blocking_work(struct work_struct *w) { (void)w;flag(&entered,1);while(!read_flag(&release_callback))IOSleep(1); }
static void count_work(struct work_struct *w) { (void)w;__atomic_add_fetch(&calls,1,__ATOMIC_SEQ_CST); }
static void cancel_thread(void *a,wait_result_t wr) { (void)a;(void)wr;cancel_work_sync(&blocked);flag(&cancel_done,1);thread_terminate(current_thread()); }
static void flush_thread(void *a,wait_result_t wr) { (void)wr;flush_workqueue(a);flag(&flush_done,1);thread_terminate(current_thread()); }
static int await_flag(int *p) { for(int i=0;i<2000;i++){if(read_flag(p))return 1;IOSleep(1);}return 0; }
int main(void) {
    work_state_lock=IOLockAlloc();
    struct workqueue_struct *a=alloc_workqueue("A",0,1),*b=alloc_workqueue("B",0,1);
    if(!a||!b)return 1;
    system_wq=a;
    INIT_WORK(&blocked,blocking_work);
    struct work_struct canceled;INIT_WORK(&canceled,count_work);
    if(!queue_work(a,&blocked)||!await_flag(&entered))return 2;
    if(!queue_work(a,&canceled)||queue_work(a,&canceled))return 3;
    if(!cancel_work_sync(&canceled)||read_flag(&calls))return 4;
    thread_t t;kernel_thread_start(cancel_thread,NULL,&t);kernel_thread_start(flush_thread,a,&t);
    IOSleep(20);
    if(read_flag(&cancel_done)||read_flag(&flush_done))return 5;
    flag(&release_callback,1);
    if(!await_flag(&cancel_done)||!await_flag(&flush_done))return 6;
    if(read_flag(&calls))return 7;
    struct delayed_work d;INIT_DELAYED_WORK(&d,count_work);
    if(!queue_delayed_work(b,&d,10)||queue_delayed_work(b,&d,10))return 8;
    d.timer.function(&d.timer);flush_work(&d.work);
    if(read_flag(&calls)!=1||d.work.wq!=b)return 9;
    if(!queue_delayed_work(b,&d,10)||!cancel_delayed_work_sync(&d))return 10;
    /* Simulate an already dispatched timer after cancellation. */
    d.timer.function(&d.timer);flush_workqueue(b);
    if(read_flag(&calls)!=1)return 11;
    if(!queue_work(a,&canceled))return 12;
    flush_work(&canceled);if(read_flag(&calls)!=2)return 13;
    destroy_workqueue(a);destroy_workqueue(b);IOLockFree(work_state_lock);
    return 0;
}
