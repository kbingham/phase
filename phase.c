#include <stdio.h>
#include <stdlib.h>

#include "phase.h"


static unsigned int failures = 0;

static unsigned int uds_residual_incorrect(int pos, int ratio)
{
	unsigned int mp = uds_multiplier(ratio);
	/* Note the incorrect multiplication by mp.
	 * This should only occur if the start phase is adjusted
	 */
	unsigned int residual = (pos * ratio * mp) % (mp * 4096);

	return residual;
}


/*
 * Satisfy UDS residual offset restriction
 *
 * The first destination pixel of any partition window must be aligned
 * to an integer multiple of the prefilter multiplier.
 *
 * To ensure this is the case we calculate a 'pull-back' offset to subtract
 * from any pixel to assert this.
 */
static unsigned int uds_residual_offset(unsigned int pos, unsigned int ratio)
{
	unsigned int mp = uds_multiplier(ratio);

	if (mp == 1) return 0;

	unsigned int n = pos * ratio / mp;
	unsigned int a = mp * n / ratio;

	//return a;

	return pos - a;

	return (pos * ratio) % mp;
}

static unsigned int uds_pullback_required(unsigned int pos, unsigned int ratio)
{
	int offset =  uds_residual_offset(pos, ratio);
	unsigned int mp = uds_multiplier(ratio);
	unsigned int residual = (pos * ratio) % (mp * 4096);

	int check = ((mp == 2 && (residual & 0x01)) || (mp == 4 && (residual & 0x03)));

	if ((!!check) != (!!offset)) {
		printf("*******uds_residual_offset (%d) and residual check %d disagree...\n", offset, check);
	}

	if (check || offset) {
		unsigned int n = pos * ratio / mp;
		unsigned int a = mp * n / ratio;

		unsigned int pullback = pos - a;

		printf("Pos: %u Ratio %u MP %u : n %u  a %u  pullback %u\n",
				pos, ratio, mp, n, a, pullback);
	}


	return check || offset;
}




/******************************************************************************
 *
 *   Prefilter multipler helper
 *
 *   Patch available to use this in UDS instead of duplicating this conditional
 */

/*
 * m' as per Table 32.31.
 * This implementation assumes BLADV is always unset.
 */
static unsigned int uds_multiplier(unsigned int ratio)
{
	/* These must be adjusted if we ever set BLADV. */
	unsigned int mp = ratio / 4096;
	return mp < 4 ? 1 : (mp < 8 ? 2 : 4);
}

/******************************************************************************
 *
 *   Phase and source pixel calculations
 *
 *   Shortened functions to do 'one' thing.
 */

/*
 *  These functions all assume a starting phase of 0.
 *  	i.e. the left edge of the image.
 */

static unsigned int uds_residual(unsigned int pos, unsigned int ratio)
{
	unsigned int mp = uds_multiplier(ratio);
	unsigned int residual = (pos * ratio) % (mp * 4096);

	return residual;
}

static unsigned int uds_left_pixel(unsigned int pos, unsigned int ratio)
{
	unsigned int mp = uds_multiplier(ratio);
	unsigned int prefilter_out = (pos * ratio) / (mp * 4096);
	unsigned int residual = (pos * ratio) % (mp * 4096);

	if ((mp == 2 && (residual & 0x01)) ||
	    (mp == 4 && (residual & 0x03))) {
		printf("WARN_ON CAUGHT in uds_left_pixel\n");
		if (failures++ > 10) {
			printf("Exiting after too many warnings\n");
			abort();
		}
	}

	return mp * (prefilter_out + (residual ? 1 : 0));
}

static unsigned int uds_right_pixel(unsigned int pos, unsigned int ratio)
{
	unsigned int mp = uds_multiplier(ratio);
	unsigned int prefilter_out = (pos * ratio) / (mp * 4096);

	return mp * (prefilter_out + 2) + (mp / 2);
}

static unsigned int uds_start_phase(unsigned int pos, unsigned int ratio)
{
	unsigned int mp = uds_multiplier(ratio);
	unsigned int residual = (pos * ratio) % (mp * 4096);

	return residual ? (4096 - residual / mp) : 0;
}

static unsigned int uds_phase_edge(unsigned int ratio)
{
	if (ratio < 4096) /* && UDS_CTRL.AMD */
		return (4096 - ratio) / 2;
	else
		return  0;
}

/*
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 *
 *   Existing UDS code
 *
 *   Taken from vsp1/uds.c
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 */

static unsigned int uds_compute_ratio(unsigned int input, unsigned int output)
{
	/* TODO: This is an approximation that will need to be refined. */
	return (input - 1) * 4096 / (output - 1);
}

static unsigned int uds_compute_ratio_naive(unsigned int input, unsigned int output)
{
	/* This one gives nice 'integer' values though ? */
	return (input) * 4096 / (output); // Interesting rounding on the ratios
}

/*
 * uds_output_size - Return the output size for an input size and scaling ratio
 * @input: input size in pixels
 * @ratio: scaling ratio in U4.12 fixed-point format
 */
static unsigned int uds_output_size(unsigned int input, unsigned int ratio)
{
	if (ratio > 4096) {
		/* Down-scaling */
		unsigned int mp = uds_multiplier(ratio);
		return (input - 1) / mp * mp * 4096 / ratio + 1;
	} else {
		/* Up-scaling */
		return (input - 1) * 4096 / ratio + 1;
	}
}

/*
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 *
 *   Phase calculations based upon the Renesas Partition Algorithm Code
 *   These calculations have been extracted from example code and refactored
 *   already to create a single function that does the common calculations.
 *
 *   This does involve performing unnecessary calculations, but reduces
 *   duplicated code.
 *
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 */


/*
 * TODO: Remove start_phase if possible:
 * 'start_phase' as we use it should always be 0 I believe,
 * Therefore this could be removed once confirmed
 */
static struct uds_phase uds_phase_calculation(unsigned int position, unsigned int start_phase,
					      unsigned int ratio)
{
	struct uds_phase phase;
	unsigned int alpha = ratio * position;

	phase.edge = uds_phase_edge(ratio);

	/* Prefilter multiplier */
	phase.mp = uds_multiplier(ratio);

	phase.prefilt_term = phase.mp * 4096;
	phase.prefilt_outpos = (alpha - start_phase * phase.mp)
			/ phase.prefilt_term;
	phase.residual = (alpha - start_phase * phase.mp) % phase.prefilt_term;

	phase.left = phase.mp * (phase.prefilt_outpos + (phase.residual ? 1 : 0));
	phase.right = phase.mp * (phase.prefilt_outpos + 2) + (phase.mp/2);

	return phase;
}

static int uds_src_left_pixel(unsigned int dstpos, unsigned int start_phase,
			      unsigned int ratio)
{
#if 0
	/* Documentation extracts */
There is a documented restriction in the data sheet at procedure 3.

<procedure3> Decide dst_pos0_pb position from temporal dst_pos0_pb
There is a restriction about the left position which UDS outputs including
discontinuous pixels. So it is necessary to shift the position of dst_pos0_pb
(temporal) to the left so that the restrictions may be satisfied. dst_pos0_pb
can be expressed as below.

	dst_pos0_pb = dst_pos0_pb (temporal) - dst_pos0_pb_shift

Here, dst_pos0_pb_shift means the number of pixels by which dst_pos0_pb
(temporal) is shifted to the left. Decide dst_pos0_pb_shift as satisfying
restriction of [(dst_pos0_pb x alpha) should be multiple of m h ’]. The
restriction can be expressed as below in other word.

	There is no restriction in case of m h ’ = 1

	(dst_pos0_pb x alpha) must be multiple of 2 in case of m h ’ = 2
	(dst_pos0_pb x alpha) must be multiple of 4 in case of m h ’ = 4

#endif
	struct uds_phase phase;

	phase = uds_phase_calculation(dstpos, start_phase, ratio);

	unsigned int pullback = uds_residual_offset(dstpos, ratio);

	/* Renesas guard against odd values in these scale ratios here ? */
	if ((phase.mp == 2 && (phase.residual & 0x01)) ||
	    (phase.mp == 4 && (phase.residual & 0x03))) {
		printf("uds_src_left_pixel: WARNING EXCEPTION CAUGHT *****\n");
		printf("P:%3u ratio %d mp:%u pt %u outpos %u residual %u edge %u src_left %u src_right %u\n",
			dstpos, ratio, phase.mp, phase.prefilt_term, phase.prefilt_outpos, phase.residual, phase.edge,
			phase.left, phase.right);

		if (failures++ > 10)
			abort();
	}

	return phase.mp * (phase.prefilt_outpos + (phase.residual ? 1 : 0));
}
