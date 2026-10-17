// Observe real changes to this daemon's Linux mount namespace. The initial
// snapshot establishes state; it is never emitted as a fabricated mount event.
#include "notifyd.h"
#include "mountwatch_state.h"
extern int __linux_syscall(int number,...);
static struct dw_mount_snapshot previous;
static int have_previous;
static int read_snapshot(struct dw_mount_snapshot* out) {
    int fd=__linux_syscall(257,-100,"/proc/self/mountinfo",0x80000,0);
    if(fd<0)return 0;
    size_t used=0,capacity=8192;char* bytes=malloc(capacity);int success=0;
    if(!bytes)goto done;
    for(;;) {
        if(used==capacity) {
            if(capacity==DW_MAX_MOUNT_BYTES)goto done;
            capacity*=2;void* enlarged=realloc(bytes,capacity);if(!enlarged)goto done;bytes=enlarged;
        }
        int count=__linux_syscall(0,fd,bytes+used,capacity-used);
        if(count==-4)continue;
        if(count<0)goto done;
        if(!count)break;
        used+=count;
    }
    success=dw_mount_parse(bytes,used,out);
done:free(bytes);__linux_syscall(3,fd);return success;
}
static void sample_mounts(void) {
    struct dw_mount_snapshot current={0};
    if(!read_snapshot(&current))return; // Preserve prior state after an incomplete read.
    if(have_previous) {
        unsigned changes=dw_mount_changes(&previous,&current);
        if(changes&DW_MOUNT_REMOVE)daemon_post("com.apple.system.kernel.unmount",0,0);
        if(changes&DW_MOUNT_ADD)daemon_post("com.apple.system.kernel.mount",0,0);
        if(changes&DW_MOUNT_UPDATE)daemon_post("com.apple.system.kernel.mountupdate",0,0);
    }
    dw_mount_free(&previous);previous=current;have_previous=1;
}
void darling_mountwatch_start(dispatch_queue_t queue) {
    sample_mounts();
    dispatch_source_t timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,queue);
    if(!timer)return;
    dispatch_source_set_timer(timer,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_MSEC*250),NSEC_PER_MSEC*250,NSEC_PER_MSEC*25);
    dispatch_source_set_event_handler(timer,^{sample_mounts();});dispatch_resume(timer);
}
