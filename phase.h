/*
 * phase.h - Prototypes to split out phase functions from test helper code.
 */

#ifndef __PHASE_H__
#define __PHASE_H__

/************************************************************************************/
/* Let me keep my 'static' functions declared static to paste back to the UDS code. */
/* Yes, I know how this looks. It's just a mocking workaround for me.               */
#define static    /* not_static */
/************************************************************************************/

/* My new helper */
static unsigned int uds_multiplier(unsigned int ratio);

/* Phase calculations destined for the UDS as necessary */
static unsigned int uds_residual(unsigned int pos, unsigned int ratio);
static unsigned int uds_residual_offset(unsigned int pos, unsigned int ratio);
static unsigned int uds_pullback_required(unsigned int pos, unsigned int ratio);
static unsigned int uds_left_pixel(unsigned int pos, unsigned int ratio);
static unsigned int uds_right_pixel(unsigned int pos, unsigned int ratio);
static unsigned int uds_start_phase(unsigned int pos, unsigned int ratio);
static unsigned int uds_phase_edge(unsigned int ratio);

/* UDS functions */
static unsigned int uds_compute_ratio(unsigned int input, unsigned int output);
static unsigned int uds_compute_ratio_naive(unsigned int input, unsigned int output);
static unsigned int uds_output_size_original(unsigned int input, unsigned int ratio);
static unsigned int uds_output_size(unsigned int input, unsigned int ratio);

/* 'Single' calculation function adaptions */
struct uds_phase {
	unsigned int mp;
	unsigned int prefilt_term;
	unsigned int prefilt_outpos;
	unsigned int residual;

	unsigned int edge;

	unsigned int left; /* Src left pixel */
	unsigned int right; /* Src right pixel */
};

static struct uds_phase uds_phase_calculation(unsigned int position,
					      unsigned int start_phase,
					      unsigned int ratio);
static int uds_src_left_pixel(unsigned int dstpos, unsigned int start_phase,
			      unsigned int ratio);

#endif // __PHASE_H__
