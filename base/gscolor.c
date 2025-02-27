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


/* Color and halftone operators for Ghostscript library */
#include "gx.h"
#include "gserrors.h"
#include "gsstruct.h"
#include "gsutil.h"		/* for gs_next_ids */
#include "gsccolor.h"
#include "gxcspace.h"
#include "gxdcconv.h"
#include "gxdevice.h"		/* for gx_color_index */
#include "gxcmap.h"
#include "gscolor2.h"
#include "gzstate.h"

/* Imported from gsht.c */
void gx_set_effective_transfer(gs_gstate *);

/* Structure descriptors */
public_st_client_color();
public_st_transfer_map();

/* GC procedures */
static
ENUM_PTRS_WITH(transfer_map_enum_ptrs, gx_transfer_map *mptr) return 0;
case 0: ENUM_RETURN((mptr->proc == 0 ? mptr->closure.data : 0));
ENUM_PTRS_END
static RELOC_PTRS_WITH(transfer_map_reloc_ptrs, gx_transfer_map *mptr)
{
    if (mptr->proc == 0)
        RELOC_PTR(gx_transfer_map, closure.data);
}
RELOC_PTRS_END

/* Initialize colors with 1, or 3, or 4 paint components. */
/* (These are only used by setcolorspace.) */
void
gx_init_paint_1(gs_client_color * pcc, const gs_color_space * pcs)
{
    pcc->paint.values[0] = 0.0;
}
void
gx_init_paint_3(gs_client_color * pcc, const gs_color_space * pcs)
{
    pcc->paint.values[2] = 0.0;
    pcc->paint.values[1] = 0.0;
    pcc->paint.values[0] = 0.0;
}
void
gx_init_paint_4(gs_client_color * pcc, const gs_color_space * pcs)
{
    /* DeviceCMYK and CIEBasedDEFG spaces initialize to 0,0,0,1. */
    pcc->paint.values[3] = 1.0;
    gx_init_paint_3(pcc, pcs);
}

/* Force a value into the range [0.0..1.0]. */
#define FORCE_UNIT(p) (p <= 0.0 ? 0.0 : p >= 1.0 ? 1.0 : p)

/* Restrict colors with 1, 3, or 4 components to the range (0,1). */
void
gx_restrict01_paint_1(gs_client_color * pcc, const gs_color_space * pcs)
{
    pcc->paint.values[0] = FORCE_UNIT(pcc->paint.values[0]);
}
void
gx_restrict01_paint_3(gs_client_color * pcc, const gs_color_space * pcs)
{
    pcc->paint.values[2] = FORCE_UNIT(pcc->paint.values[2]);
    pcc->paint.values[1] = FORCE_UNIT(pcc->paint.values[1]);
    pcc->paint.values[0] = FORCE_UNIT(pcc->paint.values[0]);
}
void
gx_restrict01_paint_4(gs_client_color * pcc, const gs_color_space * pcs)
{
    pcc->paint.values[3] = FORCE_UNIT(pcc->paint.values[3]);
    gx_restrict01_paint_3(pcc, pcs);
}

/* Null reference count adjustment procedure. */
void
gx_no_adjust_color_count(const gs_client_color * pcc,
                         const gs_color_space * pcs, int delta)
{
}

/* Forward declarations */
void load_transfer_map(gs_gstate *, gx_transfer_map *, double);

/* setgray */
int
gs_setgray(gs_gstate * pgs, double gray)
{
    gs_color_space      *pcs;
    int                 code;

    pcs = gs_cspace_new_DeviceGray(pgs->memory);
    if (pcs == NULL)
        return_error(gs_error_VMerror);
    if ((code = gs_setcolorspace(pgs, pcs)) >= 0) {
        gs_client_color *pcc = gs_currentcolor_inline(pgs);

        cs_adjust_color_count(pgs, -1); /* not strictly necessary */
        pcc->paint.values[0] = FORCE_UNIT(gray);
        pcc->pattern = 0;		/* for GC */
        gx_unset_dev_color(pgs);
    }
    rc_decrement_cs(pcs, "gs_setgray");
    return code;
}

/* setrgbcolor */
int
gs_setrgbcolor(gs_gstate * pgs, double r, double g, double b)
{
    gs_color_space      *pcs;
    int                 code;

    pcs = gs_cspace_new_DeviceRGB(pgs->memory);
    if (pcs == NULL)
        return_error(gs_error_VMerror);
    if ((code = gs_setcolorspace(pgs, pcs)) >= 0) {
       gs_client_color *pcc = gs_currentcolor_inline(pgs);

        cs_adjust_color_count(pgs, -1); /* not strictly necessary */
        pcc->paint.values[0] = FORCE_UNIT(r);
        pcc->paint.values[1] = FORCE_UNIT(g);
        pcc->paint.values[2] = FORCE_UNIT(b);
        pcc->pattern = 0;		/* for GC */
        gx_unset_dev_color(pgs);
    }
    rc_decrement_cs(pcs, "gs_setrgbcolor");
    return code;
}

/* setnullcolor */
int
gs_setnullcolor(gs_gstate * pgs)
{
    int code = 0;

    if (pgs->in_cachedevice)
        return_error(gs_error_undefined);
    code = gs_setgray(pgs, 0.0);	/* set color space to something harmless */
    color_set_null(gs_currentdevicecolor_inline(pgs));
    return code;
}

/* settransfer */
/* Remap=0 is used by the interpreter. */
int
gs_settransfer(gs_gstate * pgs, gs_mapping_proc tproc)
{
    return gs_settransfer_remap(pgs, tproc, true);
}
int
gs_settransfer_remap(gs_gstate * pgs, gs_mapping_proc tproc, bool remap)
{
    gx_transfer *ptran = &pgs->set_transfer;

    /*
     * We can safely decrement the reference counts
     * of the non-default transfer maps, because
     * if any of them get freed, the rc_unshare can't fail.
     */
    rc_decrement(ptran->red, "gs_settransfer");
    rc_decrement(ptran->green, "gs_settransfer");
    rc_decrement(ptran->blue, "gs_settransfer");
    rc_unshare_struct(ptran->gray, gx_transfer_map, &st_transfer_map,
                      pgs->memory, goto fail, "gs_settransfer");
    ptran->gray->proc = tproc;
    ptran->gray->id = gs_next_ids(pgs->memory, 1);
    ptran->red = 0;
    ptran->green = 0;
    ptran->blue = 0;
    if (remap) {
        load_transfer_map(pgs, ptran->gray, 0.0);
        gx_set_effective_transfer(pgs);
        gx_unset_dev_color(pgs);
    } else
        gx_set_effective_transfer(pgs);
    return 0;
  fail:
    rc_increment(ptran->red);
    rc_increment(ptran->green);
    rc_increment(ptran->blue);
    rc_increment(ptran->gray);
    return_error(gs_error_VMerror);
}

/* currenttransfer */
gs_mapping_proc
gs_currenttransfer(const gs_gstate * pgs)
{
    return pgs->set_transfer.gray->proc;
}

/* ------ Non-operator routines ------ */

/* Set device color = 1 for writing into the character cache. */
int
gx_set_device_color_1(gs_gstate * pgs)
{
    gs_color_space  *pcs;

    /*  We need to set the stroke *and* the fill overprint off
       to ensure the op compositor is disabled.  They will be
       restored when the graphic state is restored */
    if (pgs->stroke_overprint)
        gs_setstrokeoverprint(pgs, false);
    if (pgs->overprint)
        gs_setfilloverprint(pgs, false);

    pcs = gs_cspace_new_DeviceGray(pgs->memory);
    if (pcs) {
        gs_setcolorspace(pgs, pcs);
        rc_decrement_only_cs(pcs, "gx_set_device_color_1");
    } else {
        /* signal an error here */
        return_error(gs_error_VMerror);
    }
    set_nonclient_dev_color(gs_currentdevicecolor_inline(pgs), 1);
    pgs->log_op = lop_default;

    return 0;
}

/* ------ Internal routines ------ */

/*
 * Load one cached transfer map.  We export this for gscolor1.c.
 * Note that we must deal with both old (proc) and new (closure) maps.
 */
static float
transfer_use_proc(double value, const gx_transfer_map * pmap,
                  const void *ignore_proc_data)
{
    return (*pmap->proc) (value, pmap);
}
void
load_transfer_map(gs_gstate * pgs, gx_transfer_map * pmap, double min_value)
{
    gs_mapping_closure_proc_t proc;
    const void *proc_data;
    frac *values = pmap->values;
    frac fmin = float2frac(min_value);
    int i;

    if (pmap->proc == 0)	/* use closure */
        proc = pmap->closure.proc, proc_data = pmap->closure.data;
    else			/* use proc */
        proc = transfer_use_proc, proc_data = 0 /* not used */;
    for (i = 0; i < transfer_map_size; i++) {
        float fval =
        (*proc) ((float)i / (transfer_map_size - 1), pmap, proc_data);

        values[i] =
            (fval < min_value ? fmin :
             fval >= 1.0 ? frac_1 :
             float2frac(fval));
    }
}
