
#define DEFINE_HOOK_GLOBALS 1
#define DONT_BYPASS_HOOKS   1

#include "common.h"
#include "filter.h"
#include "hook-bind.h"

int (* __real_bind)(int fd, const struct sockaddr *sa, socklen_t sa_len);

static FilterReplyResult filter_parse_reply(FilterReplyResultBase * const rb,
                                            struct sockaddr_storage * const sa,
                                            socklen_t * const sa_len)
{
    msgpack_unpacked * const message = filter_receive_message(rb->filter);
    const msgpack_object_map * const map = &message->data.via.map;
    FilterReplyResult reply_result = filter_parse_common_reply_map(rb, map);
    
    const msgpack_object * const obj_local_host =
        msgpack_get_map_value_for_key(map, "local_host");
    if (obj_local_host != NULL &&
        obj_local_host->type == MSGPACK_OBJECT_RAW &&
        obj_local_host->via.raw.size > 0 &&
        obj_local_host->via.raw.size < NI_MAXHOST) {
        struct addrinfo *ai, hints;
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = NI_NUMERICHOST | AI_PASSIVE;
        char new_local_host[NI_MAXHOST];
        memcpy(new_local_host, obj_local_host->via.raw.ptr,
               obj_local_host->via.raw.size);
        new_local_host[obj_local_host->via.raw.size] = 0;
        const int gai_err = getaddrinfo(new_local_host, NULL, &hints, &ai);
        if (gai_err == 0) {
            assert(ai->ai_addrlen <= sizeof *sa);
            if (ai->ai_family == AF_INET) {
                assert((size_t) ai->ai_addrlen >=
                       sizeof(STORAGE_SIN_ADDR(*sa)));
                memcpy(&STORAGE_SIN_ADDR(*sa),
                       &STORAGE_SIN_ADDR(* (struct sockaddr_storage *)
                                         (void *) ai->ai_addr),
                       sizeof(STORAGE_SIN_ADDR(*sa)));
                *sa_len = ai->ai_addrlen;
                STORAGE_FAMILY(*sa) = ai->ai_family;
                SET_STORAGE_LEN(*sa, ai->ai_addrlen);
            } else if (ai->ai_family == AF_INET6) {
                assert((size_t) ai->ai_addrlen >=
                       sizeof(STORAGE_SIN_ADDR6(*sa)));
                memcpy(&STORAGE_SIN_ADDR6(*sa),
                       &STORAGE_SIN_ADDR6(* (struct sockaddr_storage *)
                                          (void *) ai->ai_addr),
                       sizeof(STORAGE_SIN_ADDR6(*sa)));
                *sa_len = ai->ai_addrlen;                
                STORAGE_FAMILY(*sa) = ai->ai_family;
                SET_STORAGE_LEN(*sa, ai->ai_addrlen);
            }
            freeaddrinfo(ai);
        }
    }    

    const msgpack_object * const obj_local_port =
        msgpack_get_map_value_for_key(map, "local_port");
    if (obj_local_port != NULL &&
        obj_local_port->type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
        if (obj_local_port->via.i64 >= 0 &&
            obj_local_port->via.i64 <= 65535) {
            if (STORAGE_FAMILY(*sa) == AF_INET) {
                STORAGE_PORT(*sa) = htons((in_port_t) obj_local_port->via.i64);
            } else if (STORAGE_FAMILY(*sa) == AF_INET6) {
                STORAGE_PORT6(*sa) = htons((in_port_t) obj_local_port->via.i64);
            }
        }
    }
    
    return reply_result;
}

static FilterReplyResult filter_apply(FilterReplyResultBase * const rb,
                                      struct sockaddr_storage * const sa,
                                      socklen_t * const sa_len)
{
    filter_before_apply(rb, 0U, "bind", sa, *sa_len, NULL, (socklen_t) 0U);
    if (filter_send_message(rb->filter) != 0) {
        return FILTER_REPLY_RESULT_ERROR;
    }    
    return filter_parse_reply(rb, sa, sa_len);
}

int __real_bind_init(void)
{
#ifdef USE_INTERPOSERS
    __real_bind = bind;
#else
    if (__real_bind == NULL) {
        __real_bind = dlsym(RTLD_NEXT, "bind");
        assert(__real_bind != NULL);        
    }
#endif
    return 0;
}

int INTERPOSE(bind)(int fd, const struct sockaddr *sa, socklen_t sa_len)
{
    __real_bind_init();
    const bool bypass_filter = getenv("SIXJACK_BYPASS") != NULL;
    int ret = 0;
    int ret_errno = 0;    
    bool bypass_call = false;
    struct sockaddr_storage sa_;
    socklen_t sa_len_ = sa_len;
    assert(sa_len <= sizeof sa_);
    memcpy(&sa_, sa, sa_len);
    FilterReplyResultBase rb = {
        .pre = true, .ret = &ret, .ret_errno = &ret_errno, .fd = fd
    };
    if (bypass_filter == false && (rb.filter = filter_get()) &&
        filter_apply(&rb, &sa_, &sa_len_) == FILTER_REPLY_BYPASS) {
        bypass_call = true;
    }
    if (bypass_call == false) {
        ret = __real_bind(fd, (struct sockaddr *) &sa_, sa_len_);
        ret_errno = errno;
    }
    if (bypass_filter == false) {
        rb.pre = false;
        filter_apply(&rb, &sa_, &sa_len_);
    }
    errno = ret_errno;
    
    return ret;
}
