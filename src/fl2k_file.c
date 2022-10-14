/*
 * osmo-fl2k, turns FL2000-based USB 3.0 to VGA adapters into
 * low cost DACs
 *
 * Copyright (C) 2016-2018 by Steve Markgraf <steve@steve-m.de>
 *
 * SPDX-License-Identifier: GPL-2.0+
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifndef _WIN32
#include <unistd.h>
#define sleep_ms(ms)	usleep(ms*1000)
#else
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include "getopt/getopt.h"
#define sleep_ms(ms)	Sleep(ms)
#endif

#include "osmo-fl2k.h"

static fl2k_dev_t *dev = NULL;

static volatile int do_exit = 0;
static volatile int repeat = 1;

uint32_t samp_rate = 100000000;

//input file
FILE *file_r;
FILE *file_g;
FILE *file_b;

//buffer for tx
char *txbuf_r = NULL;
char *txbuf_g = NULL;
char *txbuf_b = NULL;

//chanel activation
int red = 0;
int green = 0;
int blue = 0;

//enable 16 bit to 8 bit conversion
int r16 = 0;
int g16 = 0;
int b16 = 0;

//if it's a tbc
int tbcR = 0;
int tbcG = 0;
int tbcB = 0;

int sample_type = 1;// 1 == signed   0 == unsigned

uint32_t sample_cnt_r = 0;//used for tbc processing
uint32_t sample_cnt_g = 0;//used for tbc processing
uint32_t sample_cnt_b = 0;//used for tbc processing

void usage(void)
{
	fprintf(stderr,
		"fl2k_file2, a sample player for FL2K VGA dongles\n\n"
		"Usage:\n"
		"\t[-d device_index (default: 0)]\n"
		"\t[-s samplerate (default: 100 MS/s) you can write(ntsc)]\n"
		"\t[-u Set sample type to unsigned]\n"
		"\t[-R filename (use '-' to read from stdin)\n"
		"\t[-G filename (use '-' to read from stdin)\n"
		"\t[-G filename (use '-' to read from stdin)\n"
		"\t[-R16 (convert bits 16 to 8)\n"
		"\t[-G16 (convert bits 16 to 8)\n"
		"\t[-B16 (convert bits 16 to 8)\n"
		"\t[-tbcR interpret R as tbc file\n"
		"\t[-tbcG interpret G as tbc file\n"
		"\t[-tbcB interpret B as tbc file\n"
	);
	exit(1);
}

#ifdef _WIN32
BOOL WINAPI
sighandler(int signum)
{
	if (CTRL_C_EVENT == signum) {
		fprintf(stderr, "Signal caught, exiting!\n");
		fl2k_stop_tx(dev);
		do_exit = 1;
		return TRUE;
	}
	return FALSE;
}
#else
static void sighandler(int signum)
{
	fprintf(stderr, "Signal caught, exiting!\n");
	fl2k_stop_tx(dev);
	do_exit = 1;
}
#endif

int read16_to8(void *buffer, FILE *stream,int istbc,char color,uint32_t sample_rate)
{
	unsigned char tmp_buf[1310720] ;
	unsigned short *calc = malloc(2);
	
	unsigned long i = 0;
	
	//(NTSC line = 910 frame = 477750) (PAL line = 1135 frame = 709375)
	unsigned long frame_lengt = 0;
	unsigned long line_lengt = 0;
	
	int *sample_cnt = NULL;
	
	int ret = 2;
	
	if(color == 'R')
	{
		sample_cnt = &sample_cnt_r;
	}
	else if(color == 'G')
	{
		sample_cnt = &sample_cnt_g;
	}
	else if(color == 'B')
	{
		sample_cnt = &sample_cnt_b;
	}
	
	if(sample_rate == 17734475 || sample_rate == 17735845)//PAL
	{
		frame_lengt = 709375;
		line_lengt = 1135;
	}
	else if(sample_rate == 14318181 || sample_rate == 14318170)//NTSC
	{
		frame_lengt = 477750;
		line_lengt = 910;
	}
	
	//buffer used for skip 1 line
	void *skip = malloc(line_lengt);
	
	while(i < 1310720)
	{
		//if we are at then end of the frame skip one line
		if((*sample_cnt == frame_lengt) && (istbc == 1))
		{
			//skip 1 line
			fread(skip,1,line_lengt,stream);
			fread(skip,1,line_lengt,stream);
			*sample_cnt = 0;
		}
		
		fread(calc,2,1,stream);
		
		tmp_buf[i] = (*calc / 256);//convert to 8 bit
		
		i += 1;//on avance le buffer de 1
		*sample_cnt += 1;
	}
	
	memcpy(buffer, &tmp_buf[0], 1310720);
	//printf("bufer : %d \n",*buffer);
	
	if(calc)
	{
		free(calc);
	}
	
	if(skip)
	{
		free(skip);
	}
	
	return ret;
}

void fl2k_callback(fl2k_data_info_t *data_info)
{	
	static uint32_t repeat_cnt = 0;
	
	//store the number of block readed
	int r;
	int g;
	int b;

	if (data_info->device_error) {
		fprintf(stderr, "Device error, exiting.\n");
		do_exit = 1;
		return;
	}
	
	//set sign (signed = 1 , unsigned = 0)
	data_info->sampletype_signed = sample_type;
	
	//send the bufer with a size of 1310720
	if(red == 1)
	{
		data_info->r_buf = txbuf_r;
	}
	if(green == 1)
	{
		//printf("Valeur : %d %u %x\n",*txbuf_g,*txbuf_g,*txbuf_g);
		data_info->g_buf = txbuf_g;
	}
	if(blue == 1)
	{
		data_info->b_buf = txbuf_b;
	}

	//read until buffer is full
	//RED
	if(red == 1 && !feof(file_r))
	{
		if(r16 == 1)
		{
			r = read16_to8(txbuf_r,file_r,tbcR,'R',samp_rate);
		}
		else
		{
			r = fread(txbuf_r, 1, 1310720, file_r);
		}
		
		if (ferror(file_r))
		{
			fprintf(stderr, "(RED) : File Error\n");
		}
	}
	//GREEN
	if(green == 1 && !feof(file_g))
	{
		if(g16 == 1)
		{
			g = read16_to8(txbuf_g,file_g,tbcG,'G',samp_rate);
		}
		else
		{
			g = fread(txbuf_g, 1, 1310720, file_g);
		}
		
		if (ferror(file_g))
		{
			fprintf(stderr, "(GREEN) : File Error\n");
		}
	}
	//BLUE
	if(blue == 1 && !feof(file_b))
	{
		if(b16 == 1)
		{
			b = read16_to8(txbuf_b,file_b,tbcB,'B',samp_rate);
		}
		else
		{
			b = fread(txbuf_b, 1, 1310720, file_b);
		}
		
		if (ferror(file_b))
		{
			fprintf(stderr, "(GREEN) : File Error\n");
		}
	}
	
	if(((r <= 0) && (red == 1)) || ((g <= 0)  && (green == 1))|| ((b <= 0) && (blue == 1)))
	{
		fl2k_stop_tx(dev);
		do_exit = 1;
	}
}

int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact, sigign;
#endif
	int r, opt, i;
	uint32_t buf_num = 0;
	int dev_index = 0;
	void *status;

	//file adress
	char *filename_r = NULL;
	char *filename_g = NULL;
	char *filename_b = NULL;
	
	int option_index = 0;
	static struct option long_options[] = {
		{"R16", no_argument, 0, 'x'},
		{"G16", no_argument, 0, 'y'},
		{"B16", no_argument, 0, 'z'},
		{"tbcR", no_argument, 0, 'j'},
		{"tbcG", no_argument, 0, 'k'},
		{"tbcB", no_argument, 0, 'l'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long_only(argc, argv, "d:r:s:uR:G:B:",long_options, &option_index)) != -1) {
		switch (opt) {
		case 'd':
			dev_index = (uint32_t)atoi(optarg);
			break;
		case 'r':
			repeat = (int)atoi(optarg);
			break;
		case 's':
			if((strcmp(optarg, "ntsc" ) == 0) || (strcmp(optarg, "NTSC" ) == 0) || (strcmp(optarg, "Ntsc" ) == 0))
			{
				samp_rate = (uint32_t) 14318181;
			}
			else if((strcmp(optarg, "pal" ) == 0) || (strcmp(optarg, "PAL" ) == 0) || (strcmp(optarg, "Pal" ) == 0))
			{
				samp_rate = (uint32_t) 17734475;
			}
			else
			{
				samp_rate = (uint32_t)atof(optarg);
			}
			break;
		case 'u':
			sample_type = 0;
			break;
		case 'R':
			red = 1;
			filename_r = optarg;
			break;
		case 'G':
			green = 1;
			filename_g = optarg;
			break;
		case 'B':
			blue = 1;
			filename_b = optarg;
			break;
		case 'x':
			r16 = 1;
			break;
		case 'y':
			g16 = 1;
			break;
		case 'z':
			b16 = 1;
			break;
		case 'j':
			tbcR = 1;
			break;
		case 'k':
			tbcG = 1;
			break;
		case 'l':
			tbcB = 1;
			break;
		default:
			usage();
			break;
		}
	}

	if (dev_index < 0)
	{
		exit(1);
	}

	if (red == 0 && green == 0 && blue == 0)
	{
		fprintf(stderr, "\nNo file provided using option (-R,-G,-B)\n\n");
		usage();
	}
	
//RED
if(red == 1)
{
	if (strcmp(filename_r, "-") == 0) { /* Read samples from stdin */
		file_r = stdin;
#ifdef _WIN32
		_setmode(_fileno(stdin), _O_BINARY);
#endif
	} else {
		file_r = fopen(filename_r, "rb");
		if (!file_r) {
			fprintf(stderr, "(RED) : Failed to open %s\n", filename_r);
			return -ENOENT;
		}
	}

	txbuf_r = malloc(FL2K_BUF_LEN);
	if (!txbuf_r) {
		fprintf(stderr, "(RED) : malloc error!\n");
		goto out;
	}
}

//GREEN
if(green == 1)
{
	if (strcmp(filename_g, "-") == 0) { /* Read samples from stdin */
		file_g = stdin;
#ifdef _WIN32
		_setmode(_fileno(stdin), _O_BINARY);
#endif
	} else {
		file_g = fopen(filename_g, "rb");
		if (!file_g) {
			fprintf(stderr, "(GREEN) : Failed to open %s\n", filename_g);
			return -ENOENT;
		}
	}

	txbuf_g = malloc(FL2K_BUF_LEN);
	if (!txbuf_g) {
		fprintf(stderr, "(GREEN) : malloc error!\n");
		goto out;
	}
}
//BLUE
if(blue == 1)
{
	if (strcmp(filename_b, "-") == 0) { /* Read samples from stdin */
		file_b = stdin;
#ifdef _WIN32
		_setmode(_fileno(stdin), _O_BINARY);
#endif
	} else {
		file_b = fopen(filename_b, "rb");
		if (!file_b) {
			fprintf(stderr, "(BLUE) : Failed to open %s\n", filename_b);
			return -ENOENT;
		}
	}

	txbuf_b = malloc(FL2K_BUF_LEN);
	if (!txbuf_b) {
		fprintf(stderr, "(BLUE) : malloc error!\n");
		goto out;
	}
}

//next

	fl2k_open(&dev, (uint32_t)dev_index);
	if (NULL == dev) {
		fprintf(stderr, "Failed to open fl2k device #%d.\n", dev_index);
		goto out;
	}

	r = fl2k_start_tx(dev, fl2k_callback, NULL, 0);

	/* Set the sample rate */
	r = fl2k_set_sample_rate(dev, samp_rate);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set sample rate.\n");


#ifndef _WIN32
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigign.sa_handler = SIG_IGN;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigign, NULL);
#else
	SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif

	while (!do_exit)
		sleep_ms(500);

	fl2k_close(dev);

out:
//RED
if(red == 1)
{
	if (txbuf_r)
	{
		free(txbuf_r);
	}

	if (file_r && (file_r != stdin))
	{
		fclose(file_r);
	}
}
//GREEN
if(green == 1)
{
	if (txbuf_g)
	{
		free(txbuf_g);
	}

	if (file_g && (file_g != stdin))
	{
		fclose(file_g);
	}
}
//BLUE	
if(blue == 1)
{
	if (txbuf_b)
	{
		free(txbuf_b);
	}

	if (file_b && (file_b != stdin))
	{
		fclose(file_b);
	}
}

	return 0;
}
