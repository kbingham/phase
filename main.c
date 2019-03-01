#include <stdio.h>
#include <stdlib.h>

#include "phase.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct ratio {
	unsigned int input;
	unsigned int output;
} ratios[] = {
	/* Upscaling Ratio < 4096 */
	{ 1000, 32000 }, /* Ratio =  127 1:32 */
	{ 1000, 16000 }, /* Ratio =  255 1:16 */
	{  100,  1000 }, /* Ratio =  405 1:10 */
	{   10,    20 }, /* Ratio = 1940 on 1:2 */
	{  100,   200 }, /* Ratio = 2037 on 1:2 */
	{  200,   400 }, /* Ratio = 2042 on 1:2 ... leading towards 2048... */
	{ 4096,  8192 }, /* Ratio = 2047... */
	{ 8001, 16000 }, /* Ratio = 2048 */

	/* Identity scaling 1:1 */
	{    2,    2 },	/* Ratio = 4096 on 1:1 */
	{  100,  100 },	/* Ratio = 4096 on 1:1 */

	/* Downscaling Ratio > 4096 */
	{ 4096, 2048 }, /* Ratio 2:1  =  8194 (ideal world = 8192) ...? */
	{ 1920,  720 }, /* Ratio      = 10932 */
	{ 1000,  100 }, /* Ratio 10:1 = 41332 */

	/* Small values are horribly inaccurate due to the input - 1 / output - 1 */
	{   20,    2 }, /* Ratio 10:1 = 77824 - should be 40960 */
	{ 4096,  256 }, /* 1/16 - our largest downscale */
};

/*
 * Iterate the ratios table above and perform various calculations,
 * printing their results for comparison and exploration.
 */
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

		printf("In %4u : Out %5u : Ratio : %5d (%2u.%04u): Multiplier %d Margin %2d\n",
				r->input, r->output, rr, rr/4096, ((rr%4096)*10000/4096), uds_multiplier(rr), margin);
	}
}

/*
 * A quick loop to look at how the phase changes from pixel to pixel
 */
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


void phase_valid_test(unsigned int from, unsigned int to)
{
	int ratio;

	// phase_valid_ratio_test(16384); above this fails!!! rounding? integer precision?

	/*
	 * Failures start as soon as I get mp=2 or mp=4
	 * So it's a precision thing somewhere.
	 */

	//phase_valid_ratio_test(16385); // fails!!!

	for (ratio = from; ratio <= to; ratio++) {
		phase_valid_ratio_test(ratio);
	}
}

int main(int argc, char ** argv)
{

	compute_ratios();

	//compute_phases();


	/* Upscaling     1/32, to almost 1/1 (4096/4095) */
	phase_valid_test(0x7F, 0xFFF);

	/* Direct copy - 1/1 (4096 = 1.000 in 4.12) */
	phase_valid_test(0x1000, 0x1000);

	/* Downscaling - 1:1.1 up to 1:3.999 - Passes */
	phase_valid_test(0x1001, 0x3FFF);

	/* Downscaling beyond 4:1 (/4, /8, /16 /32 fail */
	phase_valid_test(16383, 65535);

	return 0;
}
