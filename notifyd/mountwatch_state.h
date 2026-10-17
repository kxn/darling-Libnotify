#ifndef DARLING_NOTIFY_MOUNTWATCH_STATE_H
#define DARLING_NOTIFY_MOUNTWATCH_STATE_H
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define DW_MOUNT_ADD 1u
#define DW_MOUNT_REMOVE 2u
#define DW_MOUNT_UPDATE 4u
#define DW_MAX_MOUNT_BYTES (4u*1024u*1024u)
#define DW_MAX_MOUNTS 16384u
struct dw_mount_record {
    uint64_t id;
    char *device,*root,*target,*options,*type,*source,*super_options;
};
struct dw_mount_snapshot { char* storage; struct dw_mount_record* records; size_t count; };
static void dw_mount_free(struct dw_mount_snapshot* s) { free(s->records);free(s->storage);memset(s,0,sizeof(*s)); }
static int dw_mount_number(const char* value,uint64_t* number) {
    if(!*value)return 0;
    *number=0;
    for(const unsigned char* p=(const unsigned char*)value;*p;p++) {
        if(*p<'0'||*p>'9'||*number>(UINT64_MAX-(*p-'0'))/10)return 0;
        *number=*number*10+(*p-'0');
    }
    return 1;
}
static int dw_mount_compare(const void* a,const void* b) {
    uint64_t x=((const struct dw_mount_record*)a)->id,y=((const struct dw_mount_record*)b)->id;
    return (x>y)-(x<y);
}
static int dw_mount_parse(const char* bytes,size_t size,struct dw_mount_snapshot* out) {
    struct dw_mount_snapshot s={0};
    memset(out,0,sizeof(*out));
    if(!size||size>DW_MAX_MOUNT_BYTES||bytes[size-1]!='\n'||memchr(bytes,0,size))return 0;
    s.storage=malloc(size+1);if(!s.storage)return 0;
    memcpy(s.storage,bytes,size);s.storage[size]=0;
    size_t capacity=32;s.records=calloc(capacity,sizeof(*s.records));if(!s.records)goto fail;
    char* line_save=NULL;
    for(char* line=strtok_r(s.storage,"\n",&line_save);line;line=strtok_r(NULL,"\n",&line_save)) {
        char* fields[64];unsigned count=0;char* field_save=NULL;
        for(char* word=strtok_r(line," ",&field_save);word;word=strtok_r(NULL," ",&field_save)) {
            if(count==64)goto fail;fields[count++]=word;
        }
        if(count<10||s.count==DW_MAX_MOUNTS)goto fail;
        unsigned split=6;while(split<count&&strcmp(fields[split],"-"))split++;
        if(split+4!=count)goto fail;
        uint64_t id,parent;
        if(!dw_mount_number(fields[0],&id)||!id||!dw_mount_number(fields[1],&parent))goto fail;
        const char* colon=strchr(fields[2],':');
        if(!colon||colon==fields[2]||!colon[1]||strchr(colon+1,':'))goto fail;
        for(const char* p=fields[2];p<colon;p++)if(*p<'0'||*p>'9')goto fail;
        uint64_t minor;if(!dw_mount_number(colon+1,&minor))goto fail;
        if(fields[3][0]!='/'||fields[4][0]!='/')goto fail;
        if(s.count==capacity) {
            capacity*=2;void* enlarged=realloc(s.records,capacity*sizeof(*s.records));if(!enlarged)goto fail;s.records=enlarged;
        }
        s.records[s.count++]=(struct dw_mount_record){id,fields[2],fields[3],fields[4],fields[5],fields[split+1],fields[split+2],fields[split+3]};
    }
    if(!s.count)goto fail;
    qsort(s.records,s.count,sizeof(*s.records),dw_mount_compare);
    for(size_t i=1;i<s.count;i++)if(s.records[i-1].id==s.records[i].id)goto fail;
    *out=s;return 1;
fail:dw_mount_free(&s);return 0;
}
static int dw_mount_identity_equal(const struct dw_mount_record* a,const struct dw_mount_record* b) {
    return !strcmp(a->device,b->device)&&!strcmp(a->root,b->root)&&!strcmp(a->target,b->target)&&!strcmp(a->type,b->type)&&!strcmp(a->source,b->source);
}
static unsigned dw_mount_changes(const struct dw_mount_snapshot* before,const struct dw_mount_snapshot* after) {
    unsigned changes=0;size_t i=0,j=0;
    while(i<before->count||j<after->count) {
        if(i==before->count){changes|=DW_MOUNT_ADD;break;}
        if(j==after->count){changes|=DW_MOUNT_REMOVE;break;}
        const struct dw_mount_record* a=&before->records[i];const struct dw_mount_record* b=&after->records[j];
        if(a->id<b->id){changes|=DW_MOUNT_REMOVE;i++;}
        else if(a->id>b->id){changes|=DW_MOUNT_ADD;j++;}
        else {
            if(!dw_mount_identity_equal(a,b))changes|=DW_MOUNT_ADD|DW_MOUNT_REMOVE;
            else if(strcmp(a->options,b->options)||strcmp(a->super_options,b->super_options))changes|=DW_MOUNT_UPDATE;
            i++;j++;
        }
    }
    return changes;
}
#endif
