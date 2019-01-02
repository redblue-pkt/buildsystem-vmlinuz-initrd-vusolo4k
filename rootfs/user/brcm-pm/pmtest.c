/*---------------------------------------------------------------------------

    Broadcom                                                     /\
    Connecting everything(R)                              _     /  \     _
    _____________________________________________________/ \   /    \   / \_
                                                            \_/      \_/

 Copyright (c) 2007-2014 Broadcom Corporation
 All rights reserved.

 Redistribution and use of this software in source and binary forms, with or
 without modification, are permitted provided that the following conditions
 are met:

 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither the name of Broadcom Corporation nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission of Broadcom Corporation.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.

 File: pmtest.c

 Description:
 Power management sample application

 ------------------------------------------------------------------------- */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <pmlib.h>

void usage(void)
{
	printf("usage: pmtest <cmd>\n");
	printf("\n");
	printf("examples:\n");
	printf("  status       show current power status\n");
	printf("  sata 1       power up SATA controller\n");
	printf("  tp1 0        power down TP1 (second CPU thread)\n");
	printf("  tp2 0        power down TP2 (third CPU thread)\n");
	printf("  tp3 0        power down TP3 (fourth CPU thread)\n");
	printf("  cpu <speed>  set CPU clock speed in kHz, rounds up\n");
	printf("  pll 1        set alternate CPU PLL mode #1\n");
	printf("  srpd 64      enable self-refresh on all MEMCs after 64 cycles\n");
	printf("  srpd 0       disable self-refresh\n");
	printf("  standby      enter passive standby\n");
	printf("  irw_halt     enter irw_halt mode\n");
	exit(1);
}

static void warn(char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "warning: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fputs("\n", stderr);
	fflush(stderr);
	va_end(ap);
}

static void fatal(char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fputs("\n", stderr);
	fflush(stderr);
	va_end(ap);
	exit(1);
}

int main(int argc, char **argv)
{
	struct brcm_pm_state state;
	struct brcm_pm_cfg cfg;
	void *brcm_pm_ctx;
	int val = 0, has_val = 0, ret;
	char *cmd, *arg;

	while ((ret = getopt(argc, argv, "h")) != -1) {
		switch (ret) {
		case 'h':
		default:
			usage();
			break;
		}
	}

	if (optind >= argc)
		usage();
	cmd = argv[optind];
	optind++;

	brcm_pm_ctx = brcm_pm_init();
	if (!brcm_pm_ctx)
		fatal("can't open PM context");
	if (brcm_pm_get_status(brcm_pm_ctx, &state) != 0)
		fatal("can't get PM state");

	if (brcm_pm_get_cfg(brcm_pm_ctx, &cfg) != 0)
		fatal("can't get PM config");
	if (brcm_pm_set_cfg(brcm_pm_ctx, &cfg) != 0)
		fatal("can't set PM config");

	if (!strcmp(cmd, "status")) {
		printf("sata:             %d\n", state.sata_status);
		printf("tp1:              %d\n", state.tp1_status);
		printf("tp2:              %d\n", state.tp2_status);
		printf("tp3:              %d\n", state.tp3_status);
		printf("cpufreq_gov:      %s\n", state.cpufreq_gov);
		if (!strcmp(state.cpufreq_gov, "userspace\n"))
			printf("cpufreq_setspeed: %d\n",
			       state.cpufreq_setspeed);
		printf("cpufreq_cur_freq: %d\n", state.cpu_base);
		printf("cpufreq_avail:    %s\n", state.cpufreq_avail);
		printf("srpd:             %d\n", state.srpd_status);
		return 0;
	}

	if (optind < argc) {
		char *endptr;
		arg = argv[optind];
		optind++;
		val = strtol(arg, &endptr, 0);
		has_val = 1;
	}

	/* second parameter is optional */

	if (!strcmp(cmd, "standby")) {
		if (has_val)
			fatal("too many arguments: PM flags are not supported");
		if (brcm_pm_suspend(brcm_pm_ctx, BRCM_PM_STANDBY) != 0)
			fatal("can't suspend");
		return 0;
	}

	if (!strcmp(cmd, "irw_halt")) {
		if (brcm_pm_suspend(brcm_pm_ctx, BRCM_PM_IRW_HALT) != 0)
			fatal("can't suspend");
		return 0;
	}

	/* second parameter is mandatory */

	if (!has_val)
		usage();

	if (!strcmp(cmd, "usb")) {
		printf("usb is no longer supported.\n");
		printf("The kernel now enables USB autosuspend by default.\n");
		return 0;
	}

	if (!strcmp(cmd, "sata")) {
		state.sata_status = val;
		if (brcm_pm_set_status(brcm_pm_ctx, &state) != 0)
			fatal("can't set PM state (SATA)");
		return 0;
	}

	if (!strcmp(cmd, "tp1")) {
		state.tp1_status = val;
		if (brcm_pm_set_status(brcm_pm_ctx, &state) != 0)
			fatal("can't set PM state (TP1)");
		return 0;
	}

	if (!strcmp(cmd, "tp2")) {
		state.tp2_status = val;
		if (brcm_pm_set_status(brcm_pm_ctx, &state) != 0)
			fatal("can't set PM state (TP2)");
		return 0;
	}

	if (!strcmp(cmd, "tp3")) {
		state.tp3_status = val;
		if (brcm_pm_set_status(brcm_pm_ctx, &state) != 0)
			fatal("can't set PM state (TP3)");
		return 0;
	}

	if (!strcmp(cmd, "srpd")) {
		state.srpd_status = val;
		if (brcm_pm_set_status(brcm_pm_ctx, &state) != 0)
			fatal("can't set PM state (SRPD)");
		return 0;
	}

	if (!strcmp(cmd, "cpu")) {
		strcpy(state.cpufreq_gov, "userspace\n");
		state.cpufreq_setspeed = val;
		if (brcm_pm_set_status(brcm_pm_ctx, &state) != 0)
			fatal("can't set PM state (CPU)");
		if (brcm_pm_get_status(brcm_pm_ctx, &state) != 0)
			fatal("can't get PM state (CPU)");
		/*
		 * CPUFreq understands requests to mean minimum speed required,
		 * so it will round up when a number is not in the list unless
		 * the requested frequency exceeds scaling_max_freq
		 */
		if (state.cpu_base > val)
			warn("frequency rounded up to %d", state.cpu_base);
		else if (state.cpu_base < val)
			warn("frequency rounded down to %d", state.cpu_base);
		return 0;
	}

	usage();
	return 1;		/* never reached */
}
