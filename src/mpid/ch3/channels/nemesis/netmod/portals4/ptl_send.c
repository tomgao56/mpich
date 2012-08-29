/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2012 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "ptl_impl.h"
#include <pmi.h>
#include <mpl_utlist.h>

#define NUMBUFS 20
#define BUFLEN  (sizeof(MPIDI_CH3_Pkt_t) + PTL_MAX_EAGER)

static ptl_md_t global_md;

typedef struct MPID_nem_ptl_sendbuf {
    ptl_md_t md;
    ptl_handle_md_t md_handle;
    struct MPID_nem_ptl_sendbuf *next;
    union {
        struct {
            MPIDI_CH3_Pkt_t hdr;
            char payload[PTL_MAX_EAGER];
        } hp; /* header+payload */
        char p[BUFLEN]; /* just payload */
    } buf;
} MPID_nem_ptl_sendbuf_t;

static MPID_nem_ptl_sendbuf_t sendbuf[NUMBUFS];
static MPID_nem_ptl_sendbuf_t *free_head = NULL;

#define FREE_EMPTY() (free_head == NULL)
#define FREE_HEAD() free_head
#define FREE_PUSH(buf_p) MPL_LL_PREPEND(free_head, buf_p)
#define FREE_POP(buf_pp) do { *(buf_pp) = free_head; MPL_LL_DELETE(free_head, free_head); } while (0)

static struct {MPID_Request *head, *tail;} send_queue;

static int send_queued(void);

static void vc_dbg_print_sendq(FILE *stream, MPIDI_VC_t *vc) {/* FIXME: write real function */ return;}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_send_init
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPID_nem_ptl_send_init(void)
{
    int mpi_errno = MPI_SUCCESS;
    int i;
    int ret;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_SEND_INIT);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_SEND_INIT);

    MPIU_Assert(BUFLEN == sizeof(sendbuf->buf));

    /* Make sure our IOV is the same as portals4's IOV */
    MPIU_Assert(sizeof(ptl_iovec_t) == sizeof(MPID_IOV));
    MPIU_Assert(((void*)&(((ptl_iovec_t*)0)->iov_base)) == ((void*)&(((MPID_IOV*)0)->MPID_IOV_BUF)));
    MPIU_Assert(((void*)&(((ptl_iovec_t*)0)->iov_len))  == ((void*)&(((MPID_IOV*)0)->MPID_IOV_LEN)));
    MPIU_Assert(sizeof(((ptl_iovec_t*)0)->iov_len) == sizeof(((MPID_IOV*)0)->MPID_IOV_LEN));
            
    for (i = 0; i < NUMBUFS; ++i) {
        sendbuf[i].md.start = &sendbuf[i].buf;
        sendbuf[i].md.length = BUFLEN;
        sendbuf[i].md.options = 0x0;
        sendbuf[i].md.eq_handle = MPIDI_nem_ptl_eq;
        sendbuf[i].md.ct_handle = PTL_CT_NONE;
        ret = PtlMDBind(MPIDI_nem_ptl_ni, &sendbuf[i].md, &sendbuf[i].md_handle);
        MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlmdbind");
        FREE_PUSH(&sendbuf[i]);
    }

    send_queue.head = send_queue.tail = NULL;

    MPID_nem_net_module_vc_dbg_print_sendq = vc_dbg_print_sendq;

    global_md.start = 0;
    global_md.length = (ptl_size_t)-1;
    global_md.options = 0x0;
    global_md.eq_handle = MPIDI_nem_ptl_eq;
    global_md.ct_handle = PTL_CT_NONE;


 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_SEND_INIT);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_send_finalize
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPID_nem_ptl_send_finalize(void)
{
    int mpi_errno = MPI_SUCCESS;
    int ret;
    int i;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_SEND_FINALIZE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_SEND_FINALIZE);

    for (i = 0; i < NUMBUFS; ++i) {
        ret = PtlMDRelease(sendbuf[i].md_handle);
        MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlmdrelease");
    }

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_SEND_FINALIZE);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME init_id
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int init_id(MPIDI_VC_t *vc)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ptl_vc_area *const vc_ptl = VC_PTL(vc);
    char *bc;
    int pmi_errno;
    int val_max_sz;
    MPIU_CHKLMEM_DECL(1);
    MPIDI_STATE_DECL(MPID_STATE_INIT_ID);

    MPIDI_FUNC_ENTER(MPID_STATE_INIT_ID);

    pmi_errno = PMI_KVS_Get_value_length_max(&val_max_sz);
    MPIU_ERR_CHKANDJUMP1(pmi_errno, mpi_errno, MPI_ERR_OTHER, "**fail", "**fail %d", pmi_errno);
    MPIU_CHKLMEM_MALLOC(bc, char *, val_max_sz, mpi_errno, "bc");

    mpi_errno = vc->pg->getConnInfo(vc->pg_rank, bc, val_max_sz, vc->pg);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPID_nem_ptl_get_id_from_bc(bc, &vc_ptl->id, &vc_ptl->pt, &vc_ptl->ptc);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    vc_ptl->id_initialized = TRUE;
    
 fn_exit:
    MPIU_CHKLMEM_FREEALL();
    MPIDI_FUNC_EXIT(MPID_STATE_INIT_ID);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_sendq_complete_with_error
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPID_nem_ptl_sendq_complete_with_error(MPIDI_VC_t *vc, int req_errno)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_SENDQ_COMPLETE_WITH_ERROR);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_SENDQ_COMPLETE_WITH_ERROR);


 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_SENDQ_COMPLETE_WITH_ERROR);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME pack_byte
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static void pack_byte(MPID_Segment *segment, MPI_Aint first, MPI_Aint last, void *buf, MPID_nem_ptl_pack_overflow_t *overflow)
{
    MPI_Aint my_last;
    MPI_Aint bytes;
    char *end;
    MPIDI_STATE_DECL(MPID_STATE_PACK_BYTE);

    MPIDI_FUNC_ENTER(MPID_STATE_PACK_BYTE);

    /* first copy out of overflow buffer */
    if (overflow->len) {
        if (overflow->len <= last-first) {
            MPIU_Memcpy(buf, &overflow->buf[overflow->offset], overflow->len);
            first += overflow->len;
            buf = (char *)buf + overflow->len;
            overflow->len = 0;
            if (last == first)
                goto fn_exit;
        } else {
            MPIU_Memcpy(buf, &overflow->buf[overflow->offset], last-first);
            overflow->offset += overflow->len - (last-first);
            overflow->len -= last-first;
            goto fn_exit;
        }
    }

    /* unpack as much as we can into buf */
    my_last = last;
    MPID_Segment_pack(segment, first, &my_last, buf);
    
    if (my_last == last)
        /* buf is completely filled */
        goto fn_exit;

    /* remember where the unfilled section starts and how large it is */
    end = &((char *)buf)[my_last-first];
    bytes = last - my_last;

    /* unpack some into the overflow */
    first = my_last;
    my_last += sizeof(overflow->buf);
    MPID_Segment_pack(segment, first, &my_last, overflow->buf);
    MPIU_Assert(my_last - first);
    
    /* fill in the rest of buf */
    MPIU_Memcpy(end, overflow->buf, bytes);

    /* save the beginning of the offset buffer and its length */
    overflow->offset = bytes;
    overflow->len = my_last-first - bytes;

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_PACK_BYTE);
    return;
}


#undef FUNCNAME
#define FUNCNAME save_iov
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static inline void save_iov(MPID_Request *sreq, void *hdr, void *data, MPIDI_msg_sz_t data_sz)
{
    int index = 0;
    MPIDI_STATE_DECL(MPID_STATE_SAVE_IOV);

    MPIDI_FUNC_ENTER(MPID_STATE_SAVE_IOV);

    MPIU_Assert(hdr || data_sz);
    
    if (hdr) {
        sreq->dev.pending_pkt = *(MPIDI_CH3_Pkt_t *)hdr;
        sreq->dev.iov[index].MPID_IOV_BUF = &sreq->dev.pending_pkt;
        sreq->dev.iov[index].MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_t);
        ++index;
    }
    if (data_sz) {
        sreq->dev.iov[index].MPID_IOV_BUF = data;
        sreq->dev.iov[index].MPID_IOV_LEN = data_sz;
        ++index;
    }
    sreq->dev.iov_count = index;
}

#undef FUNCNAME
#define FUNCNAME send_pkt
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static inline int send_pkt(MPIDI_VC_t *vc, void **vhdr_p, void **vdata_p, MPIDI_msg_sz_t *data_sz_p)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ptl_sendbuf_t *sb;
    MPID_nem_ptl_vc_area *const vc_ptl = VC_PTL(vc);
    int ret;
    MPIDI_CH3_Pkt_t **hdr_p = (MPIDI_CH3_Pkt_t **)vhdr_p;
    char **data_p = (char **)vdata_p;
    MPIDI_STATE_DECL(MPID_STATE_SEND_PKT);

    MPIDI_FUNC_ENTER(MPID_STATE_SEND_PKT);
    
    if (!vc_ptl->id_initialized) {
        mpi_errno = init_id(vc);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }

    if (MPIDI_CH3I_Sendq_empty(send_queue) && !FREE_EMPTY()) {
        MPIDI_msg_sz_t len;
        /* send header and first chunk of data */
        FREE_POP(&sb);
        sb->buf.hp.hdr = **hdr_p;
        len = *data_sz_p;
        if (len > PTL_MAX_EAGER)
            len = PTL_MAX_EAGER;
        MPIU_Memcpy(sb->buf.hp.payload, *data_p, len);
        ret = PtlPut(sb->md_handle, 0, sizeof(sb->buf.hp.hdr) + len, PTL_NO_ACK_REQ, vc_ptl->id, vc_ptl->ptc, 0, 0, sb,
                     MPIDI_Process.my_pg_rank);
        MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlput");
        MPIU_DBG_MSG_FMT(CH3_CHANNEL, VERBOSE, (MPIU_DBG_FDEST, "PtlPut(size=%lu id=(%#x,%#x) pt=%#x) sb=%p",
                                                sizeof(sb->buf.hp.hdr) + len, vc_ptl->id.phys.nid, vc_ptl->id.phys.pid,
                                                vc_ptl->ptc, sb));
        *hdr_p = NULL;
        *data_p += len;
        *data_sz_p -= len;

        /* send additional data chunks if necessary */
        while (*data_sz_p && !FREE_EMPTY()) {
            FREE_POP(&sb);
            len = *data_sz_p;
            if (len > BUFLEN)
                len = BUFLEN;
            MPIU_Memcpy(sb->buf.p, *data_p, len);
            ret = PtlPut(sb->md_handle, 0, len, PTL_NO_ACK_REQ, vc_ptl->id, vc_ptl->ptc, 0, 0, sb, MPIDI_Process.my_pg_rank);
            MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlput");
            MPIU_DBG_MSG_FMT(CH3_CHANNEL, VERBOSE, (MPIU_DBG_FDEST, "PtlPut(size=%lu id=(%#x,%#x) pt=%#x) sb=%p", len,
                                                    vc_ptl->id.phys.nid, vc_ptl->id.phys.pid, vc_ptl->ptc, sb));
            *data_p += len;
            *data_sz_p -= len;
        }
    }

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_SEND_PKT);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME send_noncontig_pkt
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int send_noncontig_pkt(MPIDI_VC_t *vc, MPID_Request *sreq, void **vhdr_p, int *complete)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ptl_sendbuf_t *sb;
    MPID_nem_ptl_vc_area *const vc_ptl = VC_PTL(vc);
    int ret;
    MPIDI_msg_sz_t last;
    MPIDI_CH3_Pkt_t **hdr_p = (MPIDI_CH3_Pkt_t **)vhdr_p;
    MPIDI_STATE_DECL(MPID_STATE_SEND_NONCONTIG_PKT);

    MPIDI_FUNC_ENTER(MPID_STATE_SEND_NONCONTIG_PKT);

    *complete = 0;
    MPID_nem_ptl_init_sreq(sreq);

    if (!vc_ptl->id_initialized) {
        mpi_errno = init_id(vc);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }

    if (MPIDI_CH3I_Sendq_empty(send_queue) && !FREE_EMPTY()) {
        /* send header and first chunk of data */
        FREE_POP(&sb);
        sb->buf.hp.hdr = **hdr_p;

        MPIU_Assert(sreq->dev.segment_first == 0);

        last = sreq->dev.segment_size;
        if (last > PTL_MAX_EAGER)
            last = PTL_MAX_EAGER;
        pack_byte(sreq->dev.segment_ptr, 0, last, sb->buf.hp.payload, &REQ_PTL(sreq)->overflow[0]);
        ret = PtlPut(sb->md_handle, 0, sizeof(sb->buf.hp.hdr) + last, PTL_NO_ACK_REQ, vc_ptl->id, vc_ptl->ptc, 0, 0, sb,
                     MPIDI_Process.my_pg_rank);
        MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlput");
        MPIU_DBG_MSG_FMT(CH3_CHANNEL, VERBOSE, (MPIU_DBG_FDEST, "PtlPut(size=%lu id=(%#x,%#x) pt=%#x) sb=%p",
                                                sizeof(sb->buf.hp.hdr) + last, vc_ptl->id.phys.nid, vc_ptl->id.phys.pid,
                                                vc_ptl->ptc, sb));
        *hdr_p = NULL;

        if (last == sreq->dev.segment_size) {
            *complete = 1;
            goto fn_exit;
        }
        
        /* send additional data chunks */
        sreq->dev.segment_first = last;

        while (!FREE_EMPTY()) {
            FREE_POP(&sb);
            
            last = sreq->dev.segment_size;
            if (last > sreq->dev.segment_first+BUFLEN)
                last = sreq->dev.segment_first+BUFLEN;

            pack_byte(sreq->dev.segment_ptr, sreq->dev.segment_first, last, sb->buf.p, &REQ_PTL(sreq)->overflow[0]);
            sreq->dev.segment_first = last;
            ret = PtlPut(sb->md_handle, 0, last - sreq->dev.segment_first, PTL_NO_ACK_REQ, vc_ptl->id, vc_ptl->ptc, 0, 0, sb,
                         MPIDI_Process.my_pg_rank);
            MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlput");
            MPIU_DBG_MSG_FMT(CH3_CHANNEL, VERBOSE, (MPIU_DBG_FDEST, "PtlPut(size=%lu id=(%#x,%#x) pt=%#x) sb=%p",
                                                    last - sreq->dev.segment_first, vc_ptl->id.phys.nid, vc_ptl->id.phys.pid,
                                                    vc_ptl->ptc, sb));

            if (last == sreq->dev.segment_size) {
                *complete = 1;
                goto fn_exit;
            }
        }
    }

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_SEND_NONCONTIG_PKT);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME enqueue_request
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int enqueue_request(MPIDI_VC_t *vc, MPID_Request *sreq)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ptl_vc_area *const vc_ptl = VC_PTL(vc);
    MPIDI_STATE_DECL(MPID_STATE_ENQUEUE_REQUEST);

    MPIDI_FUNC_ENTER(MPID_STATE_ENQUEUE_REQUEST);
    
    MPIU_DBG_MSG (CH3_CHANNEL, VERBOSE, "enqueuing");
    MPIU_Assert(FREE_EMPTY() || !MPIDI_CH3I_Sendq_empty(send_queue));
    MPIU_Assert(sreq->dev.iov_count >= 1 && sreq->dev.iov[0].MPID_IOV_LEN > 0);

    sreq->ch.vc = vc;
    sreq->dev.iov_offset = 0;

    ++(vc_ptl->num_queued_sends);
        
    if (FREE_EMPTY()) {
        MPIDI_CH3I_Sendq_enqueue(&send_queue, sreq);
    } else {
        /* there are other sends in the queue before this one: try to send from the queue */
        MPIDI_CH3I_Sendq_enqueue(&send_queue, sreq);
        mpi_errno = send_queued();
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_ENQUEUE_REQUEST);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_SendNoncontig
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPID_nem_ptl_SendNoncontig(MPIDI_VC_t *vc, MPID_Request *sreq, void *hdr, MPIDI_msg_sz_t hdr_sz)
{
    int mpi_errno = MPI_SUCCESS;
    int complete = 0;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_SENDNONCONTIG);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_SENDNONCONTIG);
    MPIU_Assert(hdr_sz <= sizeof(MPIDI_CH3_Pkt_t));
    
    mpi_errno = send_noncontig_pkt(vc, sreq, &hdr, &complete);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    
    if (complete) {
        /* sent whole message */
        int (*reqFn)(MPIDI_VC_t *, MPID_Request *, int *);
        reqFn = sreq->dev.OnDataAvail;
        if (!reqFn) {
            MPIU_Assert(MPIDI_Request_get_type(sreq) != MPIDI_REQUEST_TYPE_GET_RESP);
            MPIDI_CH3U_Request_complete(sreq);
            MPIU_DBG_MSG(CH3_CHANNEL, VERBOSE, ".... complete");
            goto fn_exit;
        } else {
            complete = 0;
            mpi_errno = reqFn(vc, sreq, &complete);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                        
            if (complete) {
                MPIU_DBG_MSG(CH3_CHANNEL, VERBOSE, ".... complete");
                goto fn_exit;
            }
            /* not completed: more to send */
        }
    }

    REQ_PTL(sreq)->noncontig = TRUE;
    save_iov(sreq, hdr, NULL, 0); /* save the header in IOV if necessary */

    /* enqueue request */
    mpi_errno = enqueue_request(vc, sreq);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_SENDNONCONTIG);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_iStartContigMsg
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPID_nem_ptl_iStartContigMsg(MPIDI_VC_t *vc, void *hdr, MPIDI_msg_sz_t hdr_sz, void *data, MPIDI_msg_sz_t data_sz,
                                   MPID_Request **sreq_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *sreq = NULL;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_ISTARTCONTIGMSG);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_ISTARTCONTIGMSG);
    MPIU_Assert(hdr_sz <= sizeof(MPIDI_CH3_Pkt_t));

    mpi_errno = send_pkt(vc, &hdr, &data, &data_sz);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    
    if (hdr == NULL && data_sz == 0) {
        /* sent whole message */
        *sreq_ptr = NULL;
        goto fn_exit;
    }
    
    /* create a request */
    sreq = MPID_Request_create();
    MPIU_Assert(sreq != NULL);
    MPIU_Object_set_ref(sreq, 2);
    sreq->kind = MPID_REQUEST_SEND;

    sreq->dev.OnDataAvail = 0;
    REQ_PTL(sreq)->noncontig = FALSE;
    save_iov(sreq, hdr, data, data_sz);

    /* enqueue request */
    mpi_errno = enqueue_request(vc, sreq);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    
    *sreq_ptr = sreq;

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_ISTARTCONTIGMSG);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_iSendContig
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPID_nem_ptl_iSendContig(MPIDI_VC_t *vc, MPID_Request *sreq, void *hdr, MPIDI_msg_sz_t hdr_sz,
                               void *data, MPIDI_msg_sz_t data_sz)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_ISENDCONTIG);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_ISENDCONTIG);
    MPIU_Assert(hdr_sz <= sizeof(MPIDI_CH3_Pkt_t));
    
    mpi_errno = send_pkt(vc, &hdr, &data, &data_sz);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    
    if (hdr == NULL && data_sz == 0) {
        /* sent whole message */
        int (*reqFn)(MPIDI_VC_t *, MPID_Request *, int *);
        reqFn = sreq->dev.OnDataAvail;
        if (!reqFn) {
            MPIU_Assert(MPIDI_Request_get_type(sreq) != MPIDI_REQUEST_TYPE_GET_RESP);
            MPIDI_CH3U_Request_complete(sreq);
            MPIU_DBG_MSG(CH3_CHANNEL, VERBOSE, ".... complete");
            goto fn_exit;
        } else {
            int complete = 0;
                        
            mpi_errno = reqFn(vc, sreq, &complete);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                        
            if (complete) {
                MPIU_DBG_MSG(CH3_CHANNEL, VERBOSE, ".... complete");
                goto fn_exit;
            }
            /* not completed: more to send */
        }
    } else {
        save_iov(sreq, hdr, data, data_sz);
    }

    REQ_PTL(sreq)->noncontig = FALSE;
    
    /* enqueue request */
    MPIU_Assert(sreq->dev.iov_count >= 1 && sreq->dev.iov[0].MPID_IOV_LEN > 0);

    mpi_errno = enqueue_request(vc, sreq);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    
 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_ISENDCONTIG);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME send_queued
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int send_queued(void)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ptl_sendbuf_t *sb;
    int ret;
    MPIDI_STATE_DECL(MPID_STATE_SEND_QUEUED);

    MPIDI_FUNC_ENTER(MPID_STATE_SEND_QUEUED);

    while (!MPIDI_CH3I_Sendq_empty(send_queue) && !FREE_EMPTY()) {
        int complete = TRUE;
        MPIDI_msg_sz_t send_len = 0;
        int i;
        MPID_Request *sreq;
        int (*reqFn)(MPIDI_VC_t *, MPID_Request *, int *);

        sreq = MPIDI_CH3I_Sendq_head(send_queue); /* don't dequeue until we're finished sending this request */
        FREE_POP(&sb);
        
        /* copy the iov */
        MPIU_Assert(sreq->dev.iov_count <= 2);
        for (i = sreq->dev.iov_offset; i < sreq->dev.iov_count + sreq->dev.iov_offset; ++i) {
            MPIDI_msg_sz_t len;
            len = sreq->dev.iov[i].iov_len;
            if (len > BUFLEN)
                len = BUFLEN;
            MPIU_Memcpy(sb->buf.p, sreq->dev.iov[i].iov_base, len);
            send_len += len;
            if (len < sreq->dev.iov[i].iov_len) {
                /* ran out of space in buffer */
                sreq->dev.iov[i].iov_base = (char *)sreq->dev.iov[i].iov_base + len;
                sreq->dev.iov[i].iov_len -= len;
                sreq->dev.iov_offset = i;
                complete = FALSE;
                break;
            }
        }

        /* copy any noncontig data if there's room left in the send buffer */
        if (send_len < BUFLEN && REQ_PTL(sreq)->noncontig) {
            MPIDI_msg_sz_t last;
            MPIU_Assert(complete); /* if complete has been set to false, there can't be any space left in the send buffer */
            last = sreq->dev.segment_size;
            if (last > sreq->dev.segment_first+BUFLEN) {
                last = sreq->dev.segment_first+BUFLEN;
                complete = FALSE;
            }
            pack_byte(sreq->dev.segment_ptr, sreq->dev.segment_first, last, sb->buf.p, &REQ_PTL(sreq)->overflow[0]);
            send_len += last - sreq->dev.segment_first;
            sreq->dev.segment_first = last;
        }
        ret = PtlPut(sb->md_handle, 0, send_len, PTL_NO_ACK_REQ, VC_PTL(sreq->ch.vc)->id, VC_PTL(sreq->ch.vc)->ptc, 0, 0, sb,
                     MPIDI_Process.my_pg_rank);
        MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlput");

        if (!complete)
            continue;
        
        /* sent all of the data */
        reqFn = sreq->dev.OnDataAvail;
        if (!reqFn) {
            MPIU_Assert(MPIDI_Request_get_type(sreq) != MPIDI_REQUEST_TYPE_GET_RESP);
            MPIDI_CH3U_Request_complete(sreq);
        } else {
            complete = 0;
            mpi_errno = reqFn(sreq->ch.vc, sreq, &complete);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);

            if (!complete)
                continue;
        }
        
        /* completed the request */
        --(VC_PTL(sreq->ch.vc)->num_queued_sends);
        MPIDI_CH3I_Sendq_dequeue(&send_queue, &sreq);
        MPIU_DBG_MSG(CH3_CHANNEL, VERBOSE, ".... complete");

        if (VC_PTL(sreq->ch.vc)->num_queued_sends == 0 && sreq->ch.vc->state == MPIDI_VC_STATE_CLOSED) {
            /* this VC is closing, if this was the last req queued for that vc, call vc_terminated() */
            mpi_errno = MPID_nem_ptl_vc_terminated(sreq->ch.vc);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
        
    }
    
 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_SEND_QUEUED);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_send_completed
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPID_nem_ptl_send_completed(MPID_nem_ptl_sendbuf_t *sb)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_SEND_COMPLETED);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_SEND_COMPLETED);

    FREE_PUSH(sb);
    if (!MPIDI_CH3I_Sendq_empty(send_queue))
        mpi_errno = send_queued();
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_SEND_COMPLETED);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}



/* Send message for either isend or issend */
#undef FUNCNAME
#define FUNCNAME send_msg
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int send_msg(ptl_hdr_data_t ssend_flag, struct MPIDI_VC *vc, const void *buf, int count, MPI_Datatype datatype, int dest,
                    int tag, MPID_Comm *comm, int context_offset, struct MPID_Request **request)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_nem_ptl_vc_area *const vc_ptl = VC_PTL(vc);
    int ret;
    MPIDI_msg_sz_t data_sz;
    int dt_contig;
    MPI_Aint dt_true_lb;
    MPID_Datatype *dt_ptr;
    MPID_Request *sreq = NULL;
    MPIU_CHKPMEM_DECL(2);
    MPIDI_STATE_DECL(MPID_STATE_SEND_MSG);

    MPIDI_FUNC_ENTER(MPID_STATE_SEND_MSG);

    MPID_nem_ptl_request_create_sreq(sreq, mpi_errno, goto fn_exit);
    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_SEND);

    if (!vc_ptl->id_initialized) {
        mpi_errno = init_id(vc);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }
    
    MPIDI_Datatype_get_info(count, datatype, dt_contig, data_sz, dt_ptr, dt_true_lb);

    if (data_sz < PTL_LARGE_THRESHOLD) {
        /* Small message.  Send all data eagerly */
        if (dt_contig) {
            ret = PtlPut(global_md, (ptl_size_t)buf, data_sz, PTL_ACK_REQ, vc_ptl->id, vc_ptl->pt,
                         NPTL_MATCH(tag, comm->context_id + context_offset), 0, sreq, NPTL_HEADER(ssend_flag, MPIDI_Process.my_pg_rank, 0));
            MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlput");
        } else {
            /* noncontig data */
            MPI_Aint last;

            sreq->dev.segment_ptr = MPID_Segment_alloc();
            MPIU_ERR_CHKANDJUMP1(sreq->dev.segment_ptr == NULL, mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %s", "MPID_Segment_alloc");
            MPID_Segment_init(buf, count, datatype, sreq->dev.segment_ptr, 0);
            sreq->dev.segment_first = 0;
            sreq->dev.segment_size = data_sz;

            last = sreq->dev.segment_size;
            sreq->dev.iov_count = MPID_IOV_LIMIT;
            MPID_Segment_pack_vector(sreq->dev.segment_ptr, sreq->dev.segment_first, &last, sreq->dev.iov, &sreq->dev.iov_count);

            if (last == sreq->dev.segment_size) {
                /* IOV is able to describe entire message */
                ptl_md_t md;
                md.start = sreq->dev.iov;
                md.length = sreq->dev.iov_count;
                md.options = PTL_IOVEC;
                md.eq_handle = MPIDI_nem_ptl_eq;
                md.ct_handle = PTL_CT_NONE;
                ret = PtlMDBind(MPIDI_nem_ptl_ni, &md, &REQ_PTL(sreq)->md);
                MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlmdbind");
                
                ret = PtlPut(REQ_PTL(sreq)->md, (ptl_size_t)buf, data_sz, PTL_ACK_REQ, vc_ptl->id, vc_ptl->pt,
                             NPTL_MATCH(tag, comm->context_id + context_offset), 0, sreq,
                             NPTL_HEADER(ssend_flag, MPIDI_Process.my_pg_rank, 0));
                MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlput");
            } else {
                /* IOV is not long enough to describe entire message */
                MPIDI_CH3U_SRBuf_alloc(sreq, data_sz);
                MPIU_ERR_CHKANDJUMP1(sreq->dev.tmpbuf_sz == 0, mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %d", data_sz);
                sreq->dev.segment_first = 0;
                last = sreq->dev.tmpbuf_sz;
                MPID_Segment_pack(sreq->dev.segment_ptr, sreq->dev.segment_first, &last, sreq->dev.tmpbuf);
                MPIU_Assert(last == sreq->dev.segment_size);
                ret = PtlPut(global_md, (ptl_size_t)sreq->dev.tmpbuf, data_sz, PTL_ACK_REQ, vc_ptl->id, vc_ptl->pt,
                             NPTL_MATCH(tag, comm->context_id + context_offset), 0, sreq,
                             NPTL_HEADER(ssend_flag, MPIDI_Process.my_pg_rank, 0));
                MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlput");
            }
        }
    } else {
        /* Large message.  Send first chunk of data and let receiver get the rest */
        if (dt_contig) {
            ptl_me_t me;

            /* create ME for buffer so receiver can issue a GET for the data */
            me.start = buf;
            me.length = data_sz;
            me.ct_handle = PTL_CT_NONE;
            me.uid = PTL_UID_ANY;
            me.options = PTL_ME_OP_GET | PTL_ME_IS_ACCESSIBLE | PTL_ME_EVENT_LINK_DISABLE | PTL_ME_EVENT_UNLINK_DISABLE;
            me.match_id = vc_ptl->id;
            me.match_bits = alloc_match_bits();
            me.ignore_bits = 0;
            me.min_free = 0;

            ret = PtlMEAppend(MPIDI_nem_ptl_ni, MPIDI_nem_ptl_control_pt, &me, PTL_PRIORITY_LIST, sreq, &REQ_PTL(sreq)->me);
            MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlmeappend");

            REQ_PTL(sreq)->large = TRUE;
            
            ret = PtlPut(global_md, (ptl_size_t)buf, PTL_LARGE_THRESHOLD, PTL_ACK_REQ, vc_ptl->id, vc_ptl->pt,
                         NPTL_MATCH(tag, comm->context_id + context_offset), 0, sreq,
                         NPTL_HEADER(ssend_flag | NPTL_LARGE, MPIDI_Process.my_pg_rank, me.match_bits));
            MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlput");
        } else {
            /* noncontig data */
            MPI_Aint last;
            int initial_iov_count, remaining_iov_count;
            ptl_me_t me;
            ptl_md_t md;
            MPIDI_msg_sz_t buf_len;
            
            sreq->dev.segment_ptr = MPID_Segment_alloc();
            MPIU_ERR_CHKANDJUMP1(sreq->dev.segment_ptr == NULL, mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %s", "MPID_Segment_alloc");
            MPID_Segment_init(buf, count, datatype, sreq->dev.segment_ptr, 0);
            sreq->dev.segment_first = 0;
            sreq->dev.segment_size = data_sz;

            last = PTL_LARGE_THRESHOLD;
            sreq->dev.iov_count = MPID_IOV_LIMIT;
            MPID_Segment_pack_vector(sreq->dev.segment_ptr, sreq->dev.segment_first, &last, sreq->dev.iov, &sreq->dev.iov_count);

            initial_iov_count = sreq->dev.iov_count;
            sreq->dev.segment_first = last;

            if (last == PTL_LARGE_THRESHOLD) {
                /* first chunk of message fits into IOV */

                if (initial_iov_count < MPID_IOV_LIMIT) {
                    /* There may be space for the rest of the message in this IOV */
                    sreq->dev.iov_count = MPID_IOV_LIMIT - sreq->dev.iov_count;
                    last = sreq->dev.segment_size;
                    
                    MPID_Segment_pack_vector(sreq->dev.segment_ptr, sreq->dev.segment_first, &last,
                                             &sreq->dev.iov[initial_iov_count], &sreq->dev.iov_count);
                    remainting_iov_count = sreq->dev.iov_count;

                    if (last == sreq->dev.segment_size) {
                        /* Entire message fit in one IOV */

                        /* Create ME for remaining data */
                        me.start = &sreq->dev.iov[initial_iov_count];
                        me.length = remainting_iov_count;
                        me.ct_handle = PTL_CT_NONE;
                        me.uid = PTL_UID_ANY;
                        me.options = ( PTL_ME_OP_GET | PTL_ME_IS_ACCESSIBLE | PTL_ME_EVENT_LINK_DISABLE |
                                       PTL_ME_EVENT_UNLINK_DISABLE | PTL_IOVEC );
                        me.match_id = vc_ptl->id;
                        me.match_bits = alloc_match_bits();
                        me.ignore_bits = 0;
                        me.min_free = 0;
                        
                        ret = PtlMEAppend(MPIDI_nem_ptl_ni, MPIDI_nem_ptl_control_pt, &me, PTL_PRIORITY_LIST, sreq,
                                          &REQ_PTL(sreq)->me);
                        MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlmeappend");

                        /* Create MD for first chunk */
                        md.start = sreq->dev.iov;
                        md.length = initial_iov_count;
                        md.options = PTL_IOVEC;
                        md.eq_handle = MPIDI_nem_ptl_eq;
                        md.ct_handle = PTL_CT_NONE;
                        ret = PtlMDBind(MPIDI_nem_ptl_ni, &md, &REQ_PTL(sreq)->md);
                        MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlmdbind");

                        REQ_PTL(sreq)->large = TRUE;
                        
                        ret = PtlPut(REQ_PTL(sreq)->md, 0, PTL_LARGE_THRESHOLD, PTL_ACK_REQ, vc_ptl->id, vc_ptl->pt,
                                     NPTL_MATCH(tag, comm->context_id + context_offset), 0, sreq,
                                     NPTL_HEADER(ssend_flag | NPTL_LARGE, MPIDI_Process.my_pg_rank, me.match_bits));
                        MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlput");

                        goto fn_exit;
                    }
                }
                /* First chunk of message fits, but the rest doesn't */
                /* Don't handle this case separately */
            }

            /* Message doesn't fit in IOV, pack into buffers */

            sreq->dev.segment_first = 0;

            /* Pack first chunk of message */
            MPIU_CHKPMEM_MALLOC(req_PTL(sreq_)->chunk_buffer, void *, sizeof(PTL_LARGE_THRESHOLD), mpi_errno, "chunk_buffer");
            last = PTL_LARGE_THRESHOLD;
            pack_byte(sreq->dev.segment_ptr, sreq->dev.segment_first, last, REQ_PTL(sreq_)->chunk_buffer[0], &REQ_PTL(sreq)->overflow[0]);
            sreq->dev.segment_first = last;
            
            /* Pack second chunk of message */
            MPIU_CHKPMEM_MALLOC(req_PTL(sreq_)->chunk_buffer, void *, sizeof(PTL_LARGE_THRESHOLD), mpi_errno, "chunk_buffer");
            last = PTL_LARGE_THRESHOLD;
            MPID_Segment_pack(sreq->dev.segment_ptr, sreq->dev.segment_first, &last, REQ_PTL(sreq_)->chunk_buffer[1]);
            buf_len = last - sreq->dev.segment_first;
            sreq->dev.segment_first = last;

            /* create ME for second chunk */
            me.start = REQ_PTL(sreq_)->chunk_buffer[1];
            me.length = buflen;
            me.ct_handle = PTL_CT_NONE;
            me.uid = PTL_UID_ANY;
            me.options = PTL_ME_OP_GET | PTL_ME_IS_ACCESSIBLE | PTL_ME_EVENT_LINK_DISABLE | PTL_ME_EVENT_UNLINK_DISABLE;
            me.match_id = vc_ptl->id;
            me.match_bits = alloc_match_bits();
            me.ignore_bits = 0;
            me.min_free = 0;
            
            ret = PtlMEAppend(MPIDI_nem_ptl_ni, MPIDI_nem_ptl_control_pt, &me, PTL_PRIORITY_LIST, sreq,
                              &REQ_PTL(sreq)->me);
            MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlmeappend");


            REQ_PTL(sreq)->large = TRUE;
                        
            ret = PtlPut(global_md, (ptl_size_t)REQ_PTL(sreq_)->chunk_buffer[0], PTL_LARGE_THRESHOLD, PTL_ACK_REQ, vc_ptl->id,
                         vc_ptl->pt, NPTL_MATCH(tag, comm->context_id + context_offset), 0, sreq,
                         NPTL_HEADER(ssend_flag | NPTL_LARGE | NPTL_MULTIPLE, MPIDI_Process.my_pg_rank, me.match_bits));
            MPIU_ERR_CHKANDJUMP(ret, mpi_errno, MPI_ERR_OTHER, "**ptlput");
        }
    }

 fn_exit:
    *request = sreq;
    MPIU_CHKPMEM_COMMIT();
    MPIDI_FUNC_EXIT(MPID_STATE_SEND_MSG);
    return mpi_errno;
 fn_fail:
    if (sreq) {
        MPIU_Object_set_ref(sreq, 0);
        MPIDI_CH3_Request_destroy(sreq);
        sreq = NULL;
    }
    MPIU_CHKPMEM_REAP();
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_isend
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPID_nem_ptl_isend(struct MPIDI_VC *vc, const void *buf, int count, MPI_Datatype datatype, int dest, int tag,
                       MPID_Comm *comm, int context_offset, struct MPID_Request **request)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_ISEND);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_ISEND);

    mpi_errno = send_msg(0, vc, buf, count, datatype, dest, tag, comm, context_offset, request);

    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_ISEND);
    return mpi_errno;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_ptl_issend
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPID_nem_ptl_issend(struct MPIDI_VC *vc, const void *buf, int count, MPI_Datatype datatype, int dest, int tag,
                        MPID_Comm *comm, int context_offset, struct MPID_Request **request)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_PTL_ISSEND);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_PTL_ISSEND);

    mpi_errno = send_msg(NPTL_SSEND, vc, buf, count, datatype, dest, tag, comm, context_offset, request);

    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_PTL_ISSEND);
    return mpi_errno;
}
