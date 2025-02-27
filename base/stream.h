/* Copyright (C) 2001-2023 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  39 Mesa Street, Suite 108A, San Francisco,
   CA 94129, USA, for further information.
*/


/* Definitions for Ghostscript stream package */
/* Requires stdio.h */

#ifndef stream_INCLUDED
#  define stream_INCLUDED

#include "scommon.h"
#include "gxiodev.h"
#include "srdline.h"

/* See scommon.h for documentation on the design of streams. */

/* ------ Stream structure definition ------ */

/*
 * We expose the stream structure definition to clients so that
 * they can get reasonable performance out of the basic operations.
 */

/* Define the "virtual" stream procedures. */

typedef struct {

    /* Store # available for reading. */
    /* Return 0 if OK, ERRC if error or not implemented. */
#define stream_proc_available(proc)\
  int proc(stream *, gs_offset_t *)
    stream_proc_available((*available));

    /* Set position. */
    /* Return 0 if OK, ERRC if error or not implemented. */
#define stream_proc_seek(proc)\
  int proc(stream *, gs_offset_t)
    stream_proc_seek((*seek));

    /* Clear buffer and, if relevant, unblock channel. */
    /* Cannot cause an error. */
#define stream_proc_reset(proc)\
  void proc(stream *)
    stream_proc_reset((*reset));

    /* Flush buffered data to output, or drain input. */
    /* Return 0 if OK, ERRC if error. */
#define stream_proc_flush(proc)\
  int proc(stream *)
    stream_proc_flush((*flush));

    /* Flush data (if writing) & close stream. */
    /* Return 0 if OK, ERRC if error. */
#define stream_proc_close(proc)\
  int proc(stream *)
    stream_proc_close((*close));

    /* Process a buffer, updating the cursor pointers. */
    /* See strimpl.h for details. */
    stream_proc_process((*process));

    /* Switch the stream to read or write mode. */
    /* false = read, true = write. */
    /* If the procedure is 0, switching is not allowed. */
#define stream_proc_switch_mode(proc)\
  int proc(stream *, bool)
    stream_proc_switch_mode((*switch_mode));

} stream_procs;

/* ------ The actual stream structure ------ */

struct stream_s {
    /*
     * To allow the stream itself to serve as the "state"
     * of a couple of heavily used types, we start its
     * definition with the common stream state.
     */
    stream_state_common;
    /*
     * The following invariants apply at all times for read streams:
     *
     *    s->cbuf - 1 <= s->cursor.r.ptr <= s->cursor.r.limit.
     *
     *    The amount of data in the buffer is s->cursor.r.limit + 1 - s->cbuf.
     *
     *    s->position represents the stream position as of the beginning
     *      of the buffer, so the current position is s->position +
     *      (s->cursor.r.ptr + 1 - s->cbuf).
     *
     * Analogous invariants apply for write streams:
     *
     *    s->cbuf - 1 <= s->cursor.w.ptr <= s->cursor.w.limit.
     *
     *    The amount of data in the buffer is s->cursor.w.ptr + 1 - s->cbuf.
     *
     *    s->position represents the stream position as of the beginning
     *      of the buffer, so the current position is s->position +
     *      (s->cursor.w.ptr + 1 - s->cbuf).
     */
    stream_cursor cursor;	/* cursor for reading/writing data */
    byte *cbuf;			/* base of buffer */
    uint bsize;			/* size of buffer, 0 if closed */
    uint cbsize;		/* size of buffer */
    /*
     * end_status indicates what should happen when the client
     * reaches the end of the buffer:
     *      0 in the normal case;
     *      EOFC if a read stream has reached EOD or a write
     *        stream has written the EOD marker;
     *      ERRC if an error terminated the last read or write
     *        operation from or to the underlying data source
     *        or sink;
     *      INTC if the last transfer was interrupted (NOT
     *        USED YET);
     *      CALLC if a callout is required.
     */
    short end_status;		/* status at end of buffer (when */
                                /* reading) or now (when writing) */
    byte foreign;		/* true if buffer is outside heap */
    byte modes;			/* access modes allowed for this stream */
#define s_mode_read 1
#define s_mode_write 2
#define s_mode_seek 4
#define s_mode_append 8		/* (s_mode_write also set) */
#define s_is_valid(s) ((s)->modes != 0)
#define s_is_reading(s) (((s)->modes & s_mode_read) != 0)
#define s_is_writing(s) (((s)->modes & s_mode_write) != 0)
#define s_can_seek(s) (((s)->modes & s_mode_seek) != 0)
    gs_string cbuf_string;	/* cbuf/cbsize if cbuf is a string, */
                                /* 0/? if not */
    gs_memory_t *cbuf_string_memory;  /* If != NULL, stream owns the string buffer */
    gs_offset_t position;		/* file position of beginning of */
                                /* buffer */
    stream_procs procs;
    stream *strm;		/* the underlying stream, non-zero */
                                /* iff this is a filter stream */
    int is_temp;		/* if >0, this is a temporary */
                                /* stream and should be freed */
                                /* when its source/sink is closed; */
                                /* if >1, the buffer is also */
                                /* temporary */
    int inline_temp;		/* temporary for inline access */
                                /* (see spgetc_inline below) */
    stream_state *state;	/* state of process */
    /*
     * The following are for the use of the interpreter.
     * See files.h for more information on read_id and write_id,
     * zfile.c for more information on prev and next,
     * zfilter.c for more information on close_strm.
     */
    ushort read_id;		/* "unique" serial # for detecting */
                                /* references to closed streams */
                                /* and for validating read access */
    ushort write_id;		/* ditto to validate write access */
    stream *prev, *next;	/* keep track of all files */
    bool close_strm;		/* CloseSource/CloseTarget */
    bool close_at_eod;		/*(default is true, only false if "reusable")*/
    int (*save_close)(stream *);	/* save original close proc */
    /*
     * In order to avoid allocating a separate stream_state for
     * file streams, which are the most heavily used stream type,
     * we put their state here.
     */
    gp_file *file;		/* file handle for C library */
    gs_const_string file_name;	/* file name (optional) -- clients must */
                                /* access only through procedures */
    uint file_modes;		/* access modes for the file, */
                                /* may be a superset of modes */
    /* Clients must only set the following through sread_subfile. */
    gs_offset_t file_offset;		/* starting point in file (reading) */
    gs_offset_t file_limit;		/* ending point in file (reading) */
};

/* The descriptor is only public for subclassing. */
extern_st(st_stream);
#define public_st_stream()	/* in stream.c */\
  gs_public_st_composite_final(st_stream, stream, "stream",\
    stream_enum_ptrs, stream_reloc_ptrs, stream_finalize)
#define STREAM_NUM_PTRS 6

#define S_FILE_LIMIT_MAX (sizeof(gs_offset_t) > 4 ? max_int64_t : max_long)

/* Initialize the checking IDs of a stream. */
#define s_init_ids(s) ((s)->read_id = (s)->write_id = 1)
#define s_init_read_id(s) ((s)->read_id = 1, (s)->write_id = 0)
#define s_init_write_id(s) ((s)->read_id = 0, (s)->write_id = 1)
#define s_init_no_id(s) ((s)->read_id = (s)->write_id = 0)

/* ------ Stream functions ------ */

/* Some of these are macros -- beware. */
/* Note that unlike the C stream library, */
/* ALL stream procedures take the stream as the first argument. */
#define sendrp(s) ((s)->cursor.r.ptr >= (s)->cursor.r.limit)	/* NOT FOR CLIENTS */
#define sendwp(s) ((s)->cursor.w.ptr >= (s)->cursor.w.limit)	/* NOT FOR CLIENTS */

/*
 * Following are valid for all streams.
 */
/* flush is NOT a no-op for read streams -- it discards data until EOF. */
/* close is NOT a no-op for non-file streams -- */
/* it actively disables them. */
/* The close routine must do a flush if needed. */
#define sseekable(s) s_can_seek(s)
int savailable(stream *, gs_offset_t *);

#define sreset(s) (*(s)->procs.reset)(s)
#define sflush(s) (*(s)->procs.flush)(s)
int sclose(stream *);
int sswitch(stream *, bool);

/*
 * Following are only valid for read streams.
 */
int spgetcc(stream *, bool);	/* bool indicates close at EOD */
#define spgetc(s) spgetcc(s, true)	/* a procedure equivalent of sgetc */
/*
 * Note that sgetc must call spgetc one byte early, because filter must read
 * ahead to detect EOD.
 *
 * In the definition of sgetc, the first alternative should read
 *      (int)(*++((s)->cursor.r.ptr))
 * but the Borland compiler generates truly atrocious code for this.
 * The SCO ODT compiler requires the first, pointless cast to int.
 */
#define sgetc(s)\
  ((int)((s)->cursor.r.limit - (s)->cursor.r.ptr > 1 ? (++((s)->cursor.r.ptr), (int)*(s)->cursor.r.ptr) : spgetc(s)))
int sgets(stream *, byte *, uint, uint *);
int sungetc(stream *, byte);	/* ERRC on error, 0 if OK */

#define sputback(s) ((s)->cursor.r.ptr--)	/* can only do this once! */
#define seofp(s) (sendrp(s) && (s)->end_status == EOFC)
#define serrorp(s) (sendrp(s) && (s)->end_status == ERRC)
int spskip(stream *, gs_offset_t, gs_offset_t *);

#define sskip(s,nskip,pskipped) spskip(s, (long)(nskip), pskipped)
/*
 * Attempt to refill the buffer of a read stream.
 * Only call this if the end_status is not EOFC,
 * and if the buffer is (nearly) empty.
 */
int s_process_read_buf(stream *);

/*
 * Following are only valid for write streams.
 */
int spputc(stream *, byte);	/* a procedure equivalent of sputc */

/*
 * The first alternative should read
 *      ((int)(*++((s)->cursor.w.ptr)=(c)))
 * but the Borland compiler generates truly atrocious code for this.
 */
#define sputc(s,c)\
  (!sendwp(s) ? (++((s)->cursor.w.ptr), *(s)->cursor.w.ptr=(c), 0) : spputc((s),(c)))
int sputs(stream *, const byte *, uint, uint *);

/*
 * Attempt to empty the buffer of a write stream.
 * Only call this if the end_status is not EOFC.
 */
int s_process_write_buf(stream *, bool);

/* Following are only valid for positionable streams. */
gs_offset_t stell(stream *);
int spseek(stream *, gs_offset_t);

#define sseek(s,pos) spseek(s, (gs_offset_t)(pos))

/* Following are for high-performance reading clients. */
/* bufptr points to the next item. */
#define sbufptr(s) ((s)->cursor.r.ptr + 1)
#define sbufavailable(s) ((s)->cursor.r.limit - (s)->cursor.r.ptr)
#define sbufskip(s, n) ((s)->cursor.r.ptr += (n), 0)
/*
 * Define the minimum amount of data that must be left in an input buffer
 * after a read operation to handle filter read-ahead, either 0 or 1
 * byte(s).
 */
#define max_min_left 1
/*
 * The stream.state min_left value is the minimum amount of data that must
 * be left in an input buffer after a read operation to handle filter
 * read-ahead. Once filters reach EOD, return 0 since read-ahead is
 * no longer relevant.
 */
#define sbuf_min_left(s) \
      ((s->end_status == EOFC || s->end_status == ERRC ? 0 : s->state->min_left))

/* The following are for very high-performance clients of read streams, */
/* who unpack the stream state into local variables. */
/* Note that any non-inline operations must do a s_end_inline before, */
/* and a s_begin_inline after. */
#define s_declare_inline(s, cp, ep)\
  register const byte *cp;\
  const byte *ep
#define s_begin_inline(s, cp, ep)\
  cp = (s)->cursor.r.ptr, ep = (s)->cursor.r.limit
#define s_end_inline(s, cp, ep)\
  (s)->cursor.r.ptr = cp
#define sbufavailable_inline(s, cp, ep)\
  (ep - cp)
#define sendbufp_inline(s, cp, ep)\
  (cp >= ep)
/* The (int) is needed to pacify the SCO ODT compiler. */
#define sgetc_inline(s, cp, ep)\
  ((int)(sendbufp_inline(s, cp, ep) ? spgetc_inline(s, cp, ep) : *++cp))
#define spgetc_inline(s, cp, ep)\
  (s_end_inline(s, cp, ep), (s)->inline_temp = spgetc(s),\
   s_begin_inline(s, cp, ep), (s)->inline_temp)
#define sputback_inline(s, cp, ep)\
  --cp

/* Allocate a stream or a stream state. */
stream *s_alloc(gs_memory_t *, client_name_t);
stream_state *s_alloc_state(gs_memory_t *, gs_memory_type_ptr_t, client_name_t);
stream *s_alloc_immovable(gs_memory_t *, client_name_t);
/*
 * Initialize a separately allocated stream or stream state, as if allocated
 * by s_alloc[_state].
 */
void s_init(stream *, gs_memory_t *);
void s_init_state(stream_state *, const stream_template *, gs_memory_t *);

/* create a stream for a file object */
int file_prepare_stream(const char *, uint, const char *,
                 uint, stream **, char[4], gs_memory_t *);

/* Set up a file stream on a gp_file.  */
int file_init_stream(stream *, gp_file *, const char *, byte *, uint);

/* Open a file stream, optionally on a gp_file. */
int file_open_stream(const char *, uint, const char *,
                 uint, stream **, gx_io_device *,
                 iodev_proc_fopen_t, gs_memory_t *);

/* Allocate and return a file stream. */
stream * file_alloc_stream(gs_memory_t *, client_name_t);

/*
 * Macros for checking file validity.
 * NOTE: in order to work around a bug in the Borland 5.0 compiler,
 * you must use file_is_invalid rather than !file_is_valid.
 */
#define file_is_valid(svar,op)\
  (svar = fptr(op), (svar->read_id | svar->write_id) == r_size(op))
#define file_is_invalid(svar,op)\
  (svar = fptr(op), (svar->read_id | svar->write_id) != r_size(op))
#define check_file(svar,op)\
  BEGIN\
    check_type(*(op), t_file);\
    if ( file_is_invalid(svar, op) ) return_error(gs_error_ioerror);\
  END

/* Close a file stream. */
int file_close_file(stream *);

int file_close_finish(stream *);

/* Disable further access on the stream by mangling the id's */
int file_close_disable(stream *);

/* Create a stream on a string or a file. */
/* String ownership retained by the caller, for example
   Postscript string objects owned by the Postscript
   interpreter
 */
void sread_string(stream *, const byte *, uint);
void sread_string_reusable(stream *, const byte *, uint);

/* The string ownership is transferred from caller to stream.
   string_mem pointer must be allocator used to allocate the
   "string" buffer.
 */
void
sread_transient_string(stream *s, gs_memory_t *string_mem, const byte *ptr, uint len);
void
sread_transient_string_reusable(stream *s, gs_memory_t *string_mem, const byte *ptr, uint len);

void swrite_string(stream *, byte *, uint);
void sread_file(stream *, gp_file *, byte *, uint),
    swrite_file(stream *, gp_file *, byte *, uint);
int  sappend_file(stream *, gp_file *, byte *, uint);

/* Confine reading to a subfile.  This is primarily for reusable streams. */
int sread_subfile(stream *s, gs_offset_t start, gs_offset_t length);

/* Set the file name of a stream, copying the name. */
/* Return <0 if the copy could not be allocated. */
int ssetfilename(stream *, const byte *, uint);

/* Return the file name of a stream, if any. */
/* There is a guaranteed 0 byte after the string. */
int sfilename(stream *, gs_const_string *);

/* Create a stream that tracks the position, */
/* for calculating how much space to allocate when actually writing. */
void swrite_position_only(stream *);

/* Standard stream initialization */
void s_std_init(stream *, byte *, uint, const stream_procs *, int /*mode */ );

/* Standard stream finalization */
void s_disable(stream *);

/* Generic stream procedures exported for templates */
int s_std_null(stream *);
void s_std_read_reset(stream *), s_std_write_reset(stream *);
int s_std_read_flush(stream *), s_std_write_flush(stream *), s_std_noavailable(stream *, gs_offset_t *),
     s_std_noseek(stream *, gs_offset_t), s_std_close(stream *), s_std_switch_mode(stream *, bool);

/* Generic procedures for filters. */
int s_filter_write_flush(stream *), s_filter_close(stream *);

/* Generic procedure structures for filters. */
extern const stream_procs s_filter_read_procs, s_filter_write_procs;

/*
 * Add a filter to an output pipeline.  The client must have allocated the
 * stream state, if any, using the given allocator.  For s_init_filter, the
 * client must have called s_init and s_init_state.
 *
 * Note that if additional buffering is needed, s_add_filter may add
 * an additional filter to provide it.
 */
int s_init_filter(stream *fs, stream_state *fss, byte *buf, uint bsize,
                  stream *target);
stream *s_add_filter(stream **ps, const stream_template *template_,
                     stream_state *ss, gs_memory_t *mem);

/*
 * Close the filters in a pipeline, up to a given target stream, freeing
 * their buffers and state structures.
 */
int s_close_filters(stream **ps, stream *target);

/* Define templates for the NullEncode/Decode filters. */
/* They have no state. */
extern const stream_template s_NullE_template;
extern const stream_template s_NullD_template;

        /* for ziodev.c */
int file_close_finish(stream *);
int file_close_disable(stream *);

#endif /* stream_INCLUDED */
