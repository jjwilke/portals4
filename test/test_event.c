#include <portals4.h>
#include <portals4_runtime.h>

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <sched.h>

#define CHECK_RETURNVAL(x) do { int ret; \
    switch (ret = x) { \
	case PTL_OK: break; \
	case PTL_FAIL: fprintf(stderr, "=> %s returned PTL_FAIL (line %u)\n", #x, (unsigned int)__LINE__); abort(); break; \
	case PTL_NO_SPACE: fprintf(stderr, "=> %s returned PTL_NO_SPACE (line %u)\n", #x, (unsigned int)__LINE__); abort(); break; \
	case PTL_ARG_INVALID: fprintf(stderr, "=> %s returned PTL_ARG_INVALID (line %u)\n", #x, (unsigned int)__LINE__); abort(); break; \
	case PTL_NO_INIT: fprintf(stderr, "=> %s returned PTL_NO_INIT (line %u)\n", #x, (unsigned int)__LINE__); abort(); break; \
	default: fprintf(stderr, "=> %s returned failcode %i (line %u)\n", #x, ret, (unsigned int)__LINE__); abort(); break; \
    } } while (0)

static void noFailures(
    ptl_handle_ct_t ct,
    ptl_size_t threshold,
    size_t line)
{
    ptl_ct_event_t ct_data;
    CHECK_RETURNVAL(PtlCTWait(ct, threshold, &ct_data));
    if (ct_data.failure != 0) {
	fprintf(stderr, "ct_data reports failure!!!!!!! {%u, %u} line %u\n",
		(unsigned int)ct_data.success, (unsigned int)ct_data.failure,
		(unsigned int)line);
	abort();
    }
}

int main(
    int argc,
    char *argv[])
{
    ptl_handle_ni_t ni_logical;
    ptl_process_t myself;
    ptl_pt_index_t logical_pt_index;
    ptl_process_t *amapping;
    uint64_t value, readval;
    ptl_le_t value_le;
    ptl_handle_le_t value_le_handle;
    ptl_md_t read_md;
    ptl_handle_md_t read_md_handle;
    ptl_handle_eq_t pt_eq_handle;
    char verb = 0;
    int my_rank, num_procs;

    CHECK_RETURNVAL(PtlInit());

    my_rank = runtime_get_rank();
    num_procs = runtime_get_size();

    amapping = malloc(sizeof(ptl_process_t) * num_procs);

    CHECK_RETURNVAL(PtlNIInit
		    (PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_LOGICAL,
		     PTL_PID_ANY, NULL, NULL, num_procs, NULL, amapping,
		     &ni_logical));
    CHECK_RETURNVAL(PtlGetId(ni_logical, &myself));
    assert(my_rank == myself.rank);
    CHECK_RETURNVAL(PtlEQAlloc(ni_logical, 100, &pt_eq_handle));
    CHECK_RETURNVAL(PtlPTAlloc
		    (ni_logical, 0, pt_eq_handle, PTL_PT_ANY,
		     &logical_pt_index));
    assert(logical_pt_index == 0);
    /* Now do the initial setup on ni_logical */
    value = myself.rank + 0xdeadbeefc0d1f1edUL;
    value_le.start = &value;
    value_le.length = sizeof(uint64_t);
    value_le.ac_id.uid = PTL_UID_ANY;
    value_le.options = PTL_LE_OP_GET | PTL_LE_EVENT_CT_GET;
    CHECK_RETURNVAL(PtlCTAlloc(ni_logical, &value_le.ct_handle));
    CHECK_RETURNVAL(PtlLEAppend
		    (ni_logical, 0, value_le, PTL_PRIORITY_LIST, NULL,
		     &value_le_handle));
    /* Now do a barrier (on ni_physical) to make sure that everyone has their
     * logical interface set up */
    runtime_barrier();
    /* don't need this anymore, so free up resources */
    free(amapping);

    /* now I can communicate between ranks with ni_logical */

    /* set up the landing pad so that I can read others' values */
    readval = 0;
    read_md.start = &readval;
    read_md.length = sizeof(uint64_t);
    read_md.options = PTL_MD_EVENT_CT_REPLY;
    read_md.eq_handle = pt_eq_handle;
    CHECK_RETURNVAL(PtlCTAlloc(ni_logical, &read_md.ct_handle));
    CHECK_RETURNVAL(PtlMDBind(ni_logical, &read_md, &read_md_handle));

    if (getenv("VERBOSE") != NULL) {
	verb = 1;
    }

    /* read rank 0's value */
    {
	ptl_ct_event_t ctc;
	ptl_process_t r0 = {.rank=0};
	CHECK_RETURNVAL(PtlGet
			(read_md_handle, myself.rank%sizeof(uint64_t), sizeof(uint64_t)-(myself.rank%sizeof(uint64_t)), r0,
			 logical_pt_index, myself.rank, (void*)(uintptr_t)(myself.rank+1), myself.rank%sizeof(uint64_t)));
	CHECK_RETURNVAL(PtlCTWait(read_md.ct_handle, 1, &ctc));
	assert(ctc.failure == 0);
    }
    printf("%i readval: %llx\n", (int)myself.rank,
	   (unsigned long long)readval);
    if (myself.rank == 0) {
	noFailures(value_le.ct_handle, num_procs, __LINE__);
    }

    {
	int fetched = 0;
	do {
	    ptl_event_t event;
	    int retval;
	    fetched = 0;
	    switch (retval = PtlEQGet(pt_eq_handle, &event)) {
		case PTL_OK:
		    fetched = 1;
		    if (verb == 1) {
			printf("%i ", (int)myself.rank);
			switch(event.type) {
			    case PTL_EVENT_GET: printf("GET: "); break;
			    case PTL_EVENT_PUT: printf("PUT: "); break;
			    case PTL_EVENT_PUT_OVERFLOW: printf("PUT OVERFLOW: "); break;
			    case PTL_EVENT_ATOMIC: printf("ATOMIC: "); break;
			    case PTL_EVENT_ATOMIC_OVERFLOW: printf("ATOMIC OVERFLOW: "); break;
			    case PTL_EVENT_REPLY: printf("REPLY: "); break;
			    case PTL_EVENT_SEND: printf("SEND: "); break;
			    case PTL_EVENT_ACK: printf("ACK: "); break;
			    case PTL_EVENT_DROPPED: printf("DROPPED: "); break;
			    case PTL_EVENT_PT_DISABLED: printf("PT DISABLED: "); break;
			    case PTL_EVENT_UNLINK: printf("UNLINK: "); break;
			    case PTL_EVENT_FREE: printf("FREE: "); break;
			    case PTL_EVENT_PROBE: printf("PROBE: "); break;
			}
		    }
		    switch(event.type) {
			case PTL_EVENT_GET:
			case PTL_EVENT_PUT:
			case PTL_EVENT_PUT_OVERFLOW:
			case PTL_EVENT_ATOMIC:
			case PTL_EVENT_ATOMIC_OVERFLOW:
			case PTL_EVENT_DROPPED:
			case PTL_EVENT_PT_DISABLED:
			case PTL_EVENT_UNLINK:
			case PTL_EVENT_FREE:
			case PTL_EVENT_PROBE:
			    /* target */
			    assert(myself.rank == 0);
			    if (verb) {
				printf("match_bits(%u), rlength(%u), mlength(%u), remote_offset(%u), start(%p,%p), user_ptr(%p), hdr_data(%u), initiator(%u), uid(%u), jid(%u), ni_fail_type(%u), pt_index(%u), atomic_op(%u), atomic_type(%u)",
				    (unsigned)event.event.tevent.match_bits,
				    (unsigned)event.event.tevent.rlength,
				    (unsigned)event.event.tevent.mlength,
				    (unsigned)event.event.tevent.remote_offset,
				    event.event.tevent.start, &value,
				    event.event.tevent.user_ptr,
				    (unsigned)event.event.tevent.hdr_data,
				    (unsigned)event.event.tevent.initiator.rank,
				    event.event.tevent.uid,
				    event.event.tevent.jid,
				    (unsigned)event.event.tevent.ni_fail_type,
				    (unsigned)event.event.tevent.pt_index,
				    (unsigned)event.event.tevent.atomic_operation,
				    (unsigned)event.event.tevent.atomic_type);
			    }
			    assert(event.event.tevent.match_bits == 0); // since this is a non-matching NI
			    assert(((char*)event.event.tevent.start)-event.event.tevent.remote_offset == (char*)&value);
			    assert(event.event.tevent.pt_index == logical_pt_index);
			    assert(event.event.tevent.ni_fail_type == PTL_NI_OK);
			    assert(event.event.tevent.mlength == event.event.tevent.rlength);
			    assert(event.event.tevent.rlength == sizeof(uint64_t)-event.event.tevent.remote_offset);
			    assert(event.event.tevent.remote_offset == event.event.tevent.initiator.rank%sizeof(uint64_t));
			    assert(event.event.tevent.user_ptr == (void*)(uintptr_t)(event.event.tevent.initiator.rank+1));
			    assert(event.event.tevent.hdr_data == 0);
			    break;
			case PTL_EVENT_REPLY:
			case PTL_EVENT_SEND:
			case PTL_EVENT_ACK:
			    /* initiator */
			    if (verb) {
				printf("mlength(%u), offset(%u), user_ptr(%p), ni_fail_type(%u)",
				    (unsigned)event.event.ievent.mlength,
				    (unsigned)event.event.ievent.offset,
				    event.event.ievent.user_ptr,
				    (unsigned)event.event.ievent.ni_fail_type);
			    }
			    assert(event.event.ievent.mlength == sizeof(uint64_t)-(myself.rank%sizeof(uint64_t)));
			    assert(event.event.ievent.offset == myself.rank%sizeof(uint64_t));
			    assert(event.event.ievent.user_ptr == (void*)(uintptr_t)(myself.rank+1));
			    assert(event.event.ievent.ni_fail_type == PTL_NI_OK);
			    break;
		    }
		    if (verb)
			printf("\n");
		    break;
		case PTL_EQ_EMPTY:
		    break;
		default:
		    CHECK_RETURNVAL(retval);
		    break;
	    }
	} while (fetched == 1);
    }

    CHECK_RETURNVAL(PtlMDRelease(read_md_handle));
    CHECK_RETURNVAL(PtlCTFree(read_md.ct_handle));
    CHECK_RETURNVAL(PtlLEUnlink(value_le_handle));
    CHECK_RETURNVAL(PtlCTFree(value_le.ct_handle));

    /* cleanup */
    CHECK_RETURNVAL(PtlPTFree(ni_logical, logical_pt_index));
    CHECK_RETURNVAL(PtlEQFree(pt_eq_handle));
    CHECK_RETURNVAL(PtlNIFini(ni_logical));
    PtlFini();

    return 0;
}