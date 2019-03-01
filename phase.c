#include <stdio.h>
#include <stdlib.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static unsigned int uds_compute_ratio(unsigned int input, unsigned int output)
{
	/* This one gives nice 'integer' values though ? */
	return (input) * 4096 / (output); // Interesting rounding on the ratios

	/* TODO: This is an approximation that will need to be refined. */
	return (input - 1) * 4096 / (output - 1);
}

/*
 * m' as per Table 32.31.
 * This implementation assumes BLADV is always unset.
 */
static unsigned int uds_multiplier(int ratio)
{
	/* These must be adjusted if we ever set BLADV. */
	unsigned int mp = ratio / 4096;
	return mp < 4 ? 1 : (mp < 8 ? 2 : 4);
}

static unsigned int uds_residual(int pos, int ratio)
{
	unsigned int mp = uds_multiplier(ratio);
	unsigned int residual = (pos * ratio * mp) % (mp * 4096);

	return residual;
}

static unsigned int uds_left_pixel(int pos, int ratio)
{
	unsigned int mp = uds_multiplier(ratio);
	unsigned int prefilter_out = (pos * ratio * mp) / (mp * 4096);
	unsigned int residual = (pos * ratio * mp) % (mp * 4096);

#define WARN_ON(n) printf("WARN_ON CAUGHT in uds_left_pixel\n")

	if ((mp == 2 && (residual & 0x01)) ||
	    (mp == 4 && (residual & 0x03)))
		WARN_ON(1);
#undef WARN_ON

	return mp * (prefilter_out + (residual ? 1 : 0));
}

static unsigned int uds_right_pixel(int pos, int ratio)
{
	unsigned int mp = uds_multiplier(ratio);
	unsigned int prefilter_out = (pos * ratio * mp) / (mp * 4096);

	return mp * (prefilter_out + 2) + (mp / 2);
}

static int uds_start_phase(int pos, int ratio)
{
	unsigned int mp = uds_multiplier(ratio);
	unsigned int residual = (pos * ratio * mp) % (mp * 4096);

	return residual ? (4096 - residual / mp) : 0;
}

static unsigned int uds_phase_edge(int ratio)
{
	if (ratio < 4096) /* && UDS_CTRL.AMD */
		return (4096 - ratio) / 2;
	else
		return  0;
}

/*
 * uds_output_size - Return the output size for an input size and scaling ratio
 * @input: input size in pixels
 * @ratio: scaling ratio in U4.12 fixed-point format
 */
static unsigned int uds_output_size_original(unsigned int input, unsigned int ratio)
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

struct uds_phase {
	unsigned int mp;
	unsigned int prefilt_term;
	unsigned int prefilt_outpos;
	unsigned int residual;

	unsigned int edge;

	unsigned int left; /* Src left pixel */
	unsigned int right; /* Src right pixel */
};


/*
 * TODO: Remove start_phase if possible:
 * 'start_phase' as we use it should always be 0 I believe,
 * Therefore this could be removed once confirmed
 */
static struct uds_phase uds_phase_calculation(int position, int start_phase,
					      int ratio)
{
	struct uds_phase phase;
	int alpha = ratio * position;

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

struct ratio {
	unsigned int input;
	unsigned int output;
} ratios[] = {
	/* Upscaling Ratio < 4096 */
	{    2,    2 },	/* Ratio = 4096 on 1:1 */
	{  100,  100 },	/* Ratio = 4096 on 1:1 */
	{   10,   20 }, /* Ratio = 1940 on 1:2 */
	{  100,  200 }, /* Ratio = 2037 on 1:2 */
	{  200,  400 }, /* Ratio = 2042 on 1:2 ... leading towards 2048... */
	{ 4096, 8192 }, /* Ratio = 2047... */
	{ 8001, 16000 }, /* Ratio = 2048 */
	{  100, 1000 }, /* Ratio 405 1:10 */
	{ 1000, 16 * 1000 }, /* Ratio 255 1:16 */
	{ 1000, 32 * 1000 }, /* Ratio 127 1:32 */

	/* Downscaling Ratio > 4096 */
	{ 1000,  100 }, /* Ratio 10:1 = 41332 */
	/* Small values are horribly inaccurate due to the input - 1 / output - 1 */
	{   20,    2 }, /* Ratio 10:1 = 77824 - should be 40960 */
	{ 4096, 2048 }, /* Ratio 2:1 = 8194 (ideal world = 8192) ...? */
	{ 1920,  720 },
};

void compute_ratios(void)
{
	struct ratio *r;

	for (int i = 0; i < ARRAY_SIZE(ratios); i++) {
		r = &ratios[i];
		unsigned int rr = uds_compute_ratio(r->input, r->output);

		/* margin = clamp_pow_2(scale) * 4; ? */
		unsigned int margin = rr < 0x200 ? 32 : /* 8 <  scale */
				      rr < 0x400 ? 16 : /* 4 <  scale <= 8 */
				      rr < 0x800 ?  8 : /* 2 <  scale <= 4 */
						    4;  /*      scale <= 2 */

		printf(" In %u : Out %u : Ratio : %d (%u.%04u): Multiplier %d Margin %d\n",
				r->input, r->output, rr, rr/4096, ((rr%4096)*10000/4096), uds_multiplier(rr), margin);
	}
}

struct phase_frames {
	unsigned int input;
	unsigned int output;
	unsigned int step;
};

static int uds_src_left_pixel(int dstpos, int start_phase, int ratio)
{
	struct uds_phase phase;

	phase = uds_phase_calculation(dstpos, start_phase, ratio);

	/* Renesas guard against odd values in these scale ratios here ? */
	/* Tehre is a documented restriction in the data sheet at procedure 3.
	 * <procedure3> Decide dst_pos0_pb position from temporal dst_pos0_pb
There is a restriction about the left position which UDS outputs including discontinuous pixels. So it's necessary to shift
the position of dst_pos0_pb (temporal) to the left so that the restrictions may be satisfied. dst_pos0_pb can be expressed
as below.
dst_pos0_pb = dst_pos0_pb (temporal) - dst_pos0_pb_shift
Here, dst_pos0_pb_shift means the number of pixels by which dst_pos0_pb (temporal) is shifted to the left. Decide
dst_pos0_pb_shift as satisfying restriction of [(dst_pos0_pb x alpha) should be multiple of m h ’]. The restriction can be
expressed as below in other word.
There is no restriction in case of m h ’ = 1
-
-
(dst_pos0_pb x alpha) must be multiple of 2 in case of m h ’ = 2
(dst_pos0_pb x alpha) must be multiple of 4 in case of m h ’ = 4
	 */

#define WARN_ON(n) printf("WARN_ON CAUGHT\n")

	if ((phase.mp == 2 && (phase.residual & 0x01)) ||
	    (phase.mp == 4 && (phase.residual & 0x03))) {
		WARN_ON(1);
		printf("P:%3u ratio %d mp:%u pt %u outpos %u residual %u edge %u src_left %u src_right %u\n",
			dstpos, ratio, phase.mp, phase.prefilt_term, phase.prefilt_outpos, phase.residual, phase.edge,
			phase.left, phase.right);
	}

	return phase.mp * (phase.prefilt_outpos + (phase.residual ? 1 : 0));
}

void compute_phases(void)
{
	unsigned int ratio = uds_compute_ratio(10, 1024);
	unsigned int start_phase = 0;

	for (int pos = 0; pos < 256; pos++) {
		struct uds_phase phase;
		unsigned int src_left_pos;
		unsigned int src_right_pos;

		phase = uds_phase_calculation(pos, start_phase, ratio);

		printf("P:%3u ratio %d mp:%u pt %u outpos %u residual %u edge %u src_left %u src_right %u\n",
			pos, ratio, phase.mp, phase.prefilt_term, phase.prefilt_outpos, phase.residual, phase.edge,
			phase.left, phase.right);
	}
}

#define ASSERT(x, msg) if (!(x)) { printf("%s\n", msg); fflush(stdout);  abort(); }

void phase_valid_ratio_test(unsigned int ratio)
{
	int pos;

	/* ratio 16384 is breaking here (4.00) */

	printf("Ratio : %d (%u.%04u)\n", ratio, ratio/4096, ((ratio%4096)*10000/4096));

	for (pos = 0; pos < 4096; pos++) {
		unsigned int start_phase = uds_start_phase(pos, ratio);
		unsigned int left = uds_left_pixel(pos, ratio);
		unsigned int right = uds_right_pixel(pos, ratio);
		unsigned int residual = uds_residual(pos, ratio);

		struct uds_phase phase = uds_phase_calculation(pos, 0, ratio);

		//if (start_phase != phase.start);
		if (left != phase.left) {
			unsigned int newleft = uds_src_left_pixel(pos, 0, ratio);
			printf("residual = %d, phase.residual = %d\n", residual, phase.residual);
			printf("newleft = %d\n", newleft);
			printf("pos %d, left %d, phase.left %d\n", pos, left, phase.left);
			return;
		}

		ASSERT(residual == phase.residual, "Failed residual");
		ASSERT(left == phase.left, "Failed Left");
		ASSERT(right == phase.right, "Failed right");
	}
}


void phase_valid_test(void)
{
	int ratio_from = 16384; // uds_compute_ratio(10, 205);
	int ratio_to = 65535 ; //uds_compute_ratio(1000, 20);

	int ratio;

	// phase_valid_ratio_test(16384); above this fails!!! rounding? integer precision?

	/*
	 * Failures start as soon as I get mp=2 or mp=4
	 * So it's a precision thing somewhere.
	 */

	phase_valid_ratio_test(16385); // fails!!!

	return;

	for (ratio = ratio_from; ratio < ratio_to; ratio++) {
		phase_valid_ratio_test(ratio);
	}
}


int AMD_test(void)
{
	unsigned int ratio = uds_compute_ratio(1024, 1024);

	printf("Ratio: %d\n", ratio);

	return 0;
}


int main(int argc, char ** argv)
{

	compute_ratios();

	//compute_phases();

	//AMD_test();

	phase_valid_test();

	printf("10 - 5 + 1 = %d\n", 10 - 5 + 1);

	return 0;
}
