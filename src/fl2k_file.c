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
#include <pthread.h>
#include <getopt.h>
#include <unistd.h>

#ifndef _WIN32
	#include <unistd.h>
	#define sleep_ms(ms)	usleep(ms*1000)
	#else
	#include <windows.h>
	#include <io.h>
	#include <fcntl.h>
	#define sleep_ms(ms)	Sleep(ms)
#endif

#define _FILE_OFFSET_BITS 64

#ifdef _WIN64
#define FSEEK fseeko64
#else
#define FSEEK fseeko
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

FILE *file2_r;
FILE *file2_g;
FILE *file2_b;
FILE *file_audio;

//buffer for tx
char *txbuf_r = NULL;
char *txbuf_g = NULL;
char *txbuf_b = NULL;

//chanel activation
int red = 0;
int green = 0;
int blue = 0;
int red2 = 0;
int green2 = 0;
int blue2 = 0;
int audio = 0;

char sync_a = 'G';

//enable 16 bit to 8 bit conversion
int r16 = 0;
int g16 = 0;
int b16 = 0;

//signed = 1/ unsigned = 0
int r_sign = 0;
int g_sign = 0;
int b_sign = 0;

//if it's a tbc
int tbcR = 0;
int tbcG = 0;
int tbcB = 0;

//ire levels change
double ire_r = 0;
double ire_g = 0;
double ire_b = 0;

//chroma gain
double c_gain_r = 1;
double c_gain_g = 1;
double c_gain_b = 1;

//signal gain  (dynamic range)
double signal_gain_r = 1;
double signal_gain_g = 1;
double signal_gain_b = 1;

//combine mode
int cmb_mode_r = 0;
int cmb_mode_g = 0;
int cmb_mode_b = 0;

//read mode
int read_mode = 0;//0 = multitthreading / 1 = hybrid (R --> GB) / 2 = hybrid (RG --> B) / 3 = sequential (R -> G -> B)

//pipe mode
char pipe_mode = 'A';

int sample_type = 1;// 1 == signed   0 == unsigned

char video_standard = 0;

uint32_t sample_cnt_r = 0;//used for tbc processing
uint32_t sample_cnt_g = 0;//used for tbc processing
uint32_t sample_cnt_b = 0;//used for tbc processing

uint32_t line_cnt_r = 0;//used for tbc processing
uint32_t line_cnt_g = 0;//used for tbc processing
uint32_t line_cnt_b = 0;//used for tbc processing

uint32_t line_sample_cnt_r = 0;//used for tbc processing
uint32_t line_sample_cnt_g = 0;//used for tbc processing
uint32_t line_sample_cnt_b = 0;//used for tbc processing

uint32_t field_cnt_r = 0;//used for tbc processing
uint32_t field_cnt_g = 0;//used for tbc processing
uint32_t field_cnt_b = 0;//used for tbc processing

//unsigned char *pipe_buf = NULL;

//thread for processing
pthread_t thread_r;
pthread_t thread_g;
pthread_t thread_b;

void usage(void)
{
	fprintf(stderr,
		"fl2k_file2, a sample player for FL2K VGA dongles\n\n"
		"Usage:\n"
		"\t[-d device_index (default: 0)]\n"
		"\t[-s samplerate (default: 100 MS/s) you can write(ntsc) or (pal)]\n"
		"\t[-u Set the output sample type of the fl2K to unsigned]\n"
		"\t[-R filename (use '-' to read from stdin)\n"
		"\t[-G filename (use '-' to read from stdin)\n"
		"\t[-B filename (use '-' to read from stdin)\n"
		"\t[-A audio file (use '-' to read from stdin)\n"
		"\t[-syncA chanel used for sync the audio file \ default : G \ value = (R ,G ,B)\n"
		"\t[-R2 secondary file to be combined with R (use '-' to read from stdin)\n"
		"\t[-G2 secondary file to be combined with G (use '-' to read from stdin)\n"
		"\t[-B2 secondary file to be combined with B (use '-' to read from stdin)\n"
		"\t[-R16 (convert bits 16 to 8)\n"
		"\t[-G16 (convert bits 16 to 8)\n"
		"\t[-B16 (convert bits 16 to 8)\n"
		"\t[-R8 interpret R input as 8 bit\n"
		"\t[-G8 interpret G input as 8 bit\n"
		"\t[-B8 interpret B input as 8 bit\n"
		"\t[-signR interpret R input as (1 = signed / 0 = unsigned) or (s = signed / u = unsigned)\n"
		"\t[-signG interpret G input as (1 = signed / 0 = unsigned) or (s = signed / u = unsigned)\n"
		"\t[-signB interpret B input as (1 = signed / 0 = unsigned) or (s = signed / u = unsigned)\n"
		"\t[-cmbModeR combine mode \ default : 0 \ value = (0 ,1)\n"
		"\t[-cmbModeG combine mode \ default : 0 \ value = (0 ,1)\n"
		"\t[-cmbModeB combine mode \ default : 0 \ value = (0 ,1)\n"
		"\t[-tbcR interpret R as tbc file\n"
		"\t[-tbcG interpret G as tbc file\n"
		"\t[-tbcB interpret B as tbc file\n"
		"\t[-not_tbcR disable tbc processing for input R file\n"
		"\t[-not_tbcG disable tbc processing for input G file\n"
		"\t[-not_tbcB disable tbc processing for input B file\n"
		"\t[-CgainR chroma gain for input R (0.0 to 6.0) (using color burst)\n"
		"\t[-CgainG chroma gain for input G (0.0 to 6.0) (using color burst)\n"
		"\t[-CgainB chroma gain for input B (0.0 to 6.0) (using color burst)\n"
		"\t[-SgainR signal gain for output R (0.5 to 2.0) (clipping white)\n"
		"\t[-SgainG signal gain for output G (0.5 to 2.0) (clipping white)\n"
		"\t[-SgainB signal gain for output B (0.5 to 2.0) (clipping white)\n"
		"\t[-ireR IRE level for input R (-50.0 to +50.0)\n"
		"\t[-ireG IRE level for input G (-50.0 to +50.0)\n"
		"\t[-ireB IRE level for input B (-50.0 to +50.0)\n"
		"\t[-FstartR seek to frame for input R\n"
		"\t[-FstartG seek to frame for input G\n"
		"\t[-FstartB seek to frame for input B\n"
		"\t[-audioOffset offset audio from a duration of x frame\n"
		"\t[-pipeMode (default = A) option : A = Audio file / R = output of R / G = output of G / B = output of B\n"
		"\t[-readMode (default = 0) option : 0 = multit-threading (RGB) / 1 = hybrid (R --> GB) / 2 = hybrid (RG --> B) / 3 = sequential (R -> G -> B)\n"
	);
	exit(1);
}

const char *get_filename_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
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

//compute number of sample to skip
unsigned long calc_nb_skip(long sample_cnt,int linelength,long frame_lengt,long bufsize)
{
	int nb_skip = 0;
	
	//on enlève ce qui reste avant le skip
	bufsize -= (frame_lengt - sample_cnt);
	nb_skip = 1;
	
	while(bufsize > 0)
	{
		bufsize -= frame_lengt;
		
		//if we can do a complet skip
		if((bufsize - linelength) > 0)
		{
			nb_skip ++;
		}
		//if we stop in the middle of a skip
		else if(((bufsize - linelength) < 0) && bufsize > 0)
		{
			nb_skip ++;
		}
	}
	return (nb_skip * linelength);//multiply for giving the number of byte to skip
}

int read_sample_file(void *inpt_color)
{
	//parametter
	void *buffer = NULL;
	FILE *stream = NULL;
	FILE *stream2 = NULL;
	FILE *streamA = NULL;
	int istbc = 0;
	char color = (char *) inpt_color;
	//uint32_t sample_rate = samp_rate;
	double *chroma_gain = NULL;
	double *ire_level = NULL;
	double signal_gain = 1;
	
	int is16 = 0;
	int is_signed = 0;
	int is_stereo = 0;
	int combine_mode = 0;
	int is_sync_a = 0;
	int use_pipe = 0;
	
	long i = 0;//counter for tmp_buf
	long y = 0;//counter for calc
	
	//(NTSC line = 910 frame = 477750) (PAL line = 1135 frame = 709375)
	unsigned long frame_lengt = 0;
	unsigned long frame_nb_line = 0;
	unsigned int v_start =0;
	unsigned int v_end =0;
	unsigned long line_lengt = 0;
	unsigned long sample_skip = 0;
	unsigned int audio_frame = 0;
	
	//COLOR BURST
	unsigned int cbust_sample = 0;
	unsigned int cbust_middle = 0;
	double cbust_count = 0;
	double cbust_offset = 0;
	unsigned int cbust_start = 0;
	unsigned int cbust_end = 0;
	
	//count
	uint32_t *sample_cnt = NULL;
	uint32_t *line_cnt = NULL;
	uint32_t *line_sample_cnt = NULL;
	uint32_t *field_cnt = NULL;

	if(color == 'R')
	{
		buffer = txbuf_r;
		stream = file_r;
		stream2 = file2_r;
		istbc = tbcR;
		sample_cnt = &sample_cnt_r;
		line_cnt = &line_cnt_r;
		line_sample_cnt = &line_sample_cnt_r;
		field_cnt = & field_cnt_r;
		chroma_gain = &c_gain_r;
		ire_level = &ire_r;
		is_stereo = red2;
		combine_mode = cmb_mode_r;
		is16 = r16;
		is_signed = r_sign;
		signal_gain = signal_gain_r;
		if(sync_a == 'R' && pipe_mode == 'A')
		{
			is_sync_a = 1;
			streamA = file_audio;
		}
		else if(pipe_mode == 'R')
		{
			use_pipe = 1;
		}
	}
	else if(color == 'G')
	{
		buffer = txbuf_g;
		stream = file_g;
		stream2 = file2_g;
		istbc = tbcG;
		sample_cnt = &sample_cnt_g;
		line_cnt = &line_cnt_g;
		line_sample_cnt = &line_sample_cnt_g;
		field_cnt = & field_cnt_g;
		chroma_gain = &c_gain_g;
		ire_level = &ire_g;
		is_stereo = green2;
		combine_mode = cmb_mode_g;
		is16 = g16;
		is_signed = g_sign;
		signal_gain = signal_gain_g;
		if(sync_a == 'G' && pipe_mode == 'A')
		{
			is_sync_a = 1;
			streamA = file_audio;
		}
		else if(pipe_mode == 'G')
		{
			use_pipe = 1;
		}
	}
	else if(color == 'B')
	{
		buffer = txbuf_b;
		stream = file_b;
		stream2 = file2_b;
		istbc = tbcB;
		sample_cnt = &sample_cnt_b;
		line_cnt = &line_cnt_b;
		line_sample_cnt = &line_sample_cnt_b;
		field_cnt = & field_cnt_b;
		chroma_gain = &c_gain_b;
		ire_level = &ire_b;
		is_stereo = blue2;
		combine_mode = cmb_mode_b;
		is16 = b16;
		is_signed = b_sign;
		signal_gain = signal_gain_b;
		if(sync_a == 'B' && pipe_mode == 'A')
		{
			is_sync_a = 1;
			streamA = file_audio;
		}
		else if(pipe_mode == 'B')
		{
			use_pipe = 1;
		}
	}
	
	//IRE
	const float ire_conv = 1.59375;// (255/160)
	const float ire_min = 63.75;//40 * (255/160)
	const float ire_new_max = 159.375;// (140 * (255/160)) - (40 * (255/160))
	const float ire_add = (*ire_level * ire_conv);
	const float ire_gain = (ire_new_max / (ire_new_max + ire_add));
	double ire_tmp = 0;
	
	if(video_standard == 'P')//PAL value multiplied by 2 if input is 16bit
	{
		frame_lengt = 709375 * (1 + is16);
		line_lengt = 1135 * (1 + is16);
		frame_nb_line = 625;
		v_start = 185 * (1 + is16);
		v_end = 1107 * (1 + is16);
		cbust_start = 98 * (1 + is16);//not set
		cbust_end = 138 * (1 + is16);//not set
		audio_frame = ((88200/25) *2);
		//sample_skip = 4 * (1 + is16);//remove 4 extra sample in pal
	}
	else if(video_standard == 'N')//NTSC value multiplied by 2 if input is 16bit
	{
		frame_lengt = 477750 * (1 + is16);
		line_lengt = 910 * (1 + is16);
		frame_nb_line = 525;
		v_start = 134 * (1 + is16);
		v_end = 894 * (1 + is16);
		cbust_start = 78 * (1 + is16);
		cbust_end = 110 * (1 + is16);
		audio_frame = ((88200/30) * 2);
	}
	
	unsigned long buf_size = (1310720 + (is16 * 1310720));
	
	if(istbc == 1)
	{
		sample_skip += calc_nb_skip(*sample_cnt,line_lengt,frame_lengt,buf_size);
	}
	
	buf_size += sample_skip;
	
	unsigned char *tmp_buf = malloc(1310720);
	unsigned char *audio_buf = malloc(audio_frame);
	char *audio_buf_signed = (void *)audio_buf;
	unsigned char *calc = malloc(buf_size);
	unsigned char *calc2 = malloc(buf_size);
	unsigned short value16 = 0;
	unsigned short value16_2 = 0;
	unsigned char value8 = 0;
	unsigned char value8_2 = 0;
	
	short *value16_signed = (void *)&value16;
	short *value16_2_signed = (void *)&value16_2;
	char *value8_signed = (void *)&value8;
	char *value8_2_signed = (void *)&value8_2;
	
	if (tmp_buf == NULL || calc == NULL)
	{
		free(tmp_buf);   // Free both in case only one was allocated
		free(calc);
		fprintf(stderr, "(%c) malloc error (tmp_buf , calc)\n",color);
		return -1;
	}
	
	if(istbc == 1)
	{
		sample_skip = calc_nb_skip(*sample_cnt,line_lengt,frame_lengt,buf_size);
	}
	
	if(is_stereo)
	{
		if(fread(calc,buf_size,1,stream) != 1 || fread(calc2,buf_size,1,stream2) != 1)
		{
			free(tmp_buf);   // Free both in case only one was allocated
			free(calc);
			free(calc2);
			fprintf(stderr, "(%c) fread error %d : ",color,errno);
			perror(NULL);
			return -1;
		}
	}
	else
	{
		if(fread(calc,buf_size,1,stream) != 1)
		{
			free(tmp_buf);   // Free both in case only one was allocated
			free(calc);
			free(calc2);
			fprintf(stderr, "(%c) fread error %d : ",color,errno);
			perror(NULL);
			return -1;
		}
	}

	while((y < buf_size) && !do_exit)
	{
		//if we are at then end of the frame skip one line
		if(*sample_cnt == frame_lengt)
		{
			if(istbc == 1)
			{
				//skip 1 line
				y += line_lengt;
				*sample_cnt = 0;
			}
			
			//write audio file to stdout only if its not a terminal
			if(isatty(STDOUT_FILENO) == 0 && is_sync_a)
			{
				//write(stdout, tmp_buf, 1310720);
				fread(audio_buf_signed,audio_frame,1,streamA);
				//write(stdout, audio_buf_signed, audio_frame);
				fwrite(audio_buf, audio_frame,1,stdout);
				fflush(stdout);
			}
		}
		
		if(is16 == 1)
		{
			value16 = ((calc[y+1] * 256) + calc[y]);
			if(is_stereo)
			{
				value16_2 = ((calc2[y+1] * 256) + calc2[y]);
			}
			y += 2;
		}
		else
		{
			value8 = calc[y];
			if(is_stereo)
			{
				value8_2 = calc2[y];
			}
			y += 1;
		}
		
		//convert 16bit to 8bit and combine
		if(is16 == 1)
		{
			if(is_stereo)
			{
				if(combine_mode == 0)//default
				{
					if((round((*value16_signed + *value16_2_signed)/ 256.0) + 128) < -128)
					{
						tmp_buf[i] = -128;
					}
					/*else if((round((*value16_signed + *value16_2_signed)/ 256.0) + 128) > 0)
					{
						tmp_buf[i] = 0;
					}*/
					else
					{
						tmp_buf[i] = round((*value16_signed + *value16_2_signed)/ 256.0) + 128;//convert to 8 bit
					}
				}
				else//mode 1
				{
					tmp_buf[i] = round(((value16 + value16_2)/2)/ 256.0);//convert to 8 bit
				}
				
			}
			else
			{
				tmp_buf[i] = round(value16 / 256.0);//convert to 8 bit
			}
		}
		else if(is_stereo)//combine 2 file
		{
			if(combine_mode == 0)//default
			{
				tmp_buf[i] = *value8_signed + *value8_2_signed + 128;
			}
			else//mode 1
			{
				tmp_buf[i] = round((value8 + value8_2)/2);
			}
		}
		else//no processing
		{
			tmp_buf[i] = value8;
		}
		
		if(*chroma_gain != 0)
		{
			//color burst reading
			if(((*line_sample_cnt >= cbust_start) && (*line_sample_cnt <= cbust_end)) && (*line_cnt == (21 + ((unsigned long)*field_cnt % 2))))
			{
				cbust_sample = tmp_buf[i];
				cbust_count += 1;
				cbust_middle = cbust_sample / cbust_count;
				cbust_offset = (cbust_middle - (cbust_middle / *chroma_gain));
			}
			
			//chroma gain
			
			if(((*line_sample_cnt >= cbust_start) && (*line_sample_cnt <= cbust_end * 1 + is16))&& (*line_cnt > (22 + ((unsigned long)*field_cnt % 2))))
			{
				tmp_buf[i] = round(tmp_buf[i] / *chroma_gain);// + cbust_offset;
			}
		}
		
		//ire 7.5 to ire 0
		if (*ire_level != 0)
		{
			if(((*line_sample_cnt >= v_start) && (*line_sample_cnt <= v_end))&& *line_cnt > (22 + ((unsigned long)*field_cnt % 2)) )
			{
				ire_tmp = (tmp_buf[i] - ire_min);
				
				if(ire_tmp < 0)//clipping value
				{
					ire_tmp = 0;
				}
				ire_tmp = ire_tmp * ire_gain;
				tmp_buf[i] =  round(ire_tmp + ire_add + ire_min);
			}
		}
		
		if(tmp_buf[i] > 5)
		{
			if((tmp_buf[i] * signal_gain) > 255)
			{
				tmp_buf[i] = 255;
			}
			else
			{
				tmp_buf[i] = round(tmp_buf[i] * signal_gain);
			}
		}
		
		if(!is_signed)
		{
			tmp_buf[i] = tmp_buf[i] - 128;
		}
		
		i += 1;//on avance tmp_buff de 1
		
		if(*line_cnt == ((frame_nb_line / 2) + ((unsigned long)*field_cnt % 2)))//field 1 = (max - 0.5)   field 2 = (max + 0.5)
		{
			*line_cnt = 0;
			*field_cnt += 1;
			cbust_sample = 0;
			cbust_middle = 0;
			cbust_count = 0;
		}
		
		if(*line_sample_cnt == line_lengt)
		{
			*line_sample_cnt = 0;
			*line_cnt += 1;
		}
		
		*sample_cnt += (1 + is16);
		*line_sample_cnt += (1 + is16);
	}
	
	memcpy(buffer, tmp_buf, 1310720);
	if(isatty(STDOUT_FILENO) == 0 && use_pipe)
	{
		fwrite(tmp_buf, 1310720,1,stdout);
		fflush(stdout);
	}
	
	free(tmp_buf);
	free(audio_buf);
	free(calc);
	free(calc2);
	
	if(color == 'R')
	{
		pthread_exit(thread_r);
	}
	else if(color == 'G')
	{
		pthread_exit(thread_g);
	}
	else if(color == 'B')
	{
		pthread_exit(thread_b);
	}
	return 0;
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
		pthread_create(&thread_r,NULL,read_sample_file,'R');
		
		if (ferror(file_r))
		{
			fprintf(stderr, "(RED) : File Error\n");
		}
	}
	else if(red == 1 && feof(file_r))
	{
		fprintf(stderr, "(RED) : Nothing more to read\n");
	}
	
	//thread sync R
	if(read_mode == 3 || read_mode == 1)
	{
		if(red == 1)
		{
			pthread_join(thread_r,NULL);
		}
	}
	
	//GREEN
	if(green == 1 && !feof(file_g))
	{
		pthread_create(&thread_g,NULL,read_sample_file,'G');
		
		if (ferror(file_g))
		{
			fprintf(stderr, "(GREEN) : File Error\n");
		}
	}
	else if(green == 1 && feof(file_g))
	{
		fprintf(stderr, "(GREEN) : Nothing more to read\n");
	}
	
	//thread sync G
	if(read_mode == 3)
	{
		if(green == 1)
		{
			pthread_join(thread_g,NULL);
		}
	}
	else if(read_mode == 2)
	{
		if(red == 1)
		{
			pthread_join(thread_r,NULL);
		}
		if(green == 1)
		{
			pthread_join(thread_g,NULL);
		}
	}
	
	//BLUE
	if(blue == 1 && !feof(file_b))
	{
		pthread_create(&thread_b,NULL,read_sample_file,'B');
		
		if(ferror(file_b))
		{
			fprintf(stderr, "(BLUE) : File Error\n");
		}
	}
	else if(blue == 1 && feof(file_b))
	{
		fprintf(stderr, "(BLUE) : Nothing more to read\n");
	}
	
	//thread sync B
	if(read_mode == 0)
	{
		if(red == 1)
		{
			pthread_join(thread_r,NULL);
		}
		if(green == 1)
		{
			pthread_join(thread_g,NULL);
		}
		if(blue == 1)
		{
			pthread_join(thread_b,NULL);
		}
	}
	else if(read_mode == 3 || read_mode == 2)
	{
		if(blue == 1)
		{
			pthread_join(thread_b,NULL);
		}
	}
	else if(read_mode == 1)
	{
		if(green == 1)
		{
			pthread_join(thread_g,NULL);
		}
		if(blue == 1)
		{
			pthread_join(thread_b,NULL);
		}
	}
	
	/*if(!(green == 1 && feof(file_g)) || !(green == 1 && feof(file_g)) && !(green == 1 && feof(file_g)))
	{
		
	}*/
	/*if(((r <= 0) && (red == 1)) || ((g <= 0)  && (green == 1))|| ((b <= 0) && (blue == 1)))
	{
		fl2k_stop_tx(dev);
		do_exit = 1;
	}*/
}

int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact, sigign;
#endif

#ifdef _WIN32 || _WIN64
	_setmode(_fileno(stdout), O_BINARY);
	_setmode(_fileno(stdin), O_BINARY);	
#endif

	int r, opt, i;
	uint32_t buf_num = 0;
	int dev_index = 0;
	void *status;
	
	int override_r16 = -1;
	int override_g16 = -1;
	int override_b16 = -1;
	int override_r_sign = -1;
	int override_g_sign = -1;
	int override_b_sign = -1;
	int override_tbc_r = -1;
	int override_tbc_g = -1;
	int override_tbc_b = -1;
	
	uint64_t start_r = 0;
	uint64_t start_g = 0;
	uint64_t start_b = 0;
	uint64_t start_audio = 0;
	
	long audio_offset = 0;

	//file adress
	char *filename_r = NULL;
	char *filename_g = NULL;
	char *filename_b = NULL;
	
	char *filename2_r = NULL;
	char *filename2_g = NULL;
	char *filename2_b = NULL;
	char *filename_audio = NULL;
	
	//pipe_buf = malloc(1310720);
	
	/*if (pipe_buf == NULL)
	{
		free(pipe_buf);   // Free both in case only one was allocated
		fprintf(stderr, "malloc error (pipe_buf)\n");
		return -1;
	}*/
	
	//setvbuf(stdout,pipe_buf,_IOLBF,1310720);
	
	int option_index = 0;
	static struct option long_options[] = {
		{"R16", 0, 0, 1},
		{"G16", 0, 0, 2},
		{"B16", 0, 0, 3},
		{"tbcR", 0, 0, 4},
		{"tbcG", 0, 0, 5},
		{"tbcB", 0, 0, 6},
		{"readMode", 1, 0, 7},
		{"CgainR", 1, 0, 8},
		{"CgainG", 1, 0, 9},
		{"CgainB", 1, 0, 10},
		{"ireR", 1, 0, 11},
		{"ireG", 1, 0, 12},
		{"ireB", 1, 0, 13},
		{"FstartR", 1, 0, 14},
		{"FstartG", 1, 0, 15},
		{"FstartB", 1, 0, 16},
		{"FstartA", 1, 0, 16},
		{"R2", 1, 0, 17},
		{"G2", 1, 0, 18},
		{"B2", 1, 0, 19},
		{"syncA", 1, 0, 20},
		{"cmbModeR", 1, 0, 21},
		{"cmbModeG", 1, 0, 22},
		{"cmbModeB", 1, 0, 23},
		{"audioOffset", 1, 0, 24},
		{"pipeMode", 1, 0, 25},
		{"SgainR", 1, 0, 26},
		{"SgainG", 1, 0, 27},
		{"SgainB", 1, 0, 28},
		{"R8", 0, 0, 29},
		{"G8", 0, 0, 30},
		{"B8", 0, 0, 31},
		{"not_tbcR", 0, 0, 32},
		{"not_tbcG", 0, 0, 33},
		{"not_tbcB", 0, 0, 34},
		{"signR", 1, 0, 35},
		{"signG", 1, 0, 36},
		{"signB", 1, 0, 37},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long_only(argc, argv, "d:r:s:uR:G:B:A:",long_options, &option_index)) != -1) {
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
		case 'A':
			audio = 1;
			filename_audio = optarg;
			break;
		case 1:
			override_r16 = 1;
			break;
		case 2:
			override_g16 = 1;
			break;
		case 3:
			override_b16 = 1;
			break;
		case 4:
			override_tbc_r = 1;
			break;
		case 5:
			override_tbc_g = 1;
			break;
		case 6:
			override_tbc_b = 1;
			break;
		case 7:
			read_mode = (int)atoi(optarg);
			break;
		case 8:
			c_gain_r = atof(optarg);
			break;
		case 9:
			c_gain_g = atof(optarg);
			break;
		case 10:
			c_gain_b = atof(optarg);
			break;
		case 11:
			ire_r = atof(optarg);
			break;
		case 12:
			ire_g = atof(optarg);
			break;
		case 13:
			ire_b = atof(optarg);
			break;
		case 14:
			start_r = atoi(optarg);
			break;
		case 15:
			start_g = atoi(optarg);
			break;
		case 16:
			start_b = atoi(optarg);
			break;
		case 17:
			red2 = 1;
			filename2_r = optarg;
			break;
		case 18:
			green2 = 1;
			filename2_g = optarg;
			break;
		case 19:
			blue2 = 1;
			filename2_b = optarg;
			break;
		case 20:
			if(*optarg == 'r'){sync_a = 'R';}
			else if(*optarg == 'g'){sync_a = 'G';}
			else if(*optarg == 'b'){sync_a = 'B';}
			else{sync_a = *optarg;}
			break;
		case 21:
			cmb_mode_r = atoi(optarg);
			break;
		case 22:
			cmb_mode_g = atoi(optarg);
			break;
		case 23:
			cmb_mode_b = atoi(optarg);
			break;
		case 24:
			audio_offset = atol(optarg);
			break;
		case 25:
			if(*optarg == 'a'){pipe_mode = 'A';}
			else if(*optarg == 'r'){pipe_mode = 'R';}
			else if(*optarg == 'g'){pipe_mode = 'G';}
			else if(*optarg == 'b'){pipe_mode = 'B';}
			else{pipe_mode = *optarg;}
			break;
		case 26:
			signal_gain_r = atof(optarg);
			break;
		case 27:
			signal_gain_g = atof(optarg);
			break;
		case 28:
			signal_gain_b = atof(optarg);
			break;
		case 29:
			override_r16 = 0;
			break;
		case 30:
			override_g16 = 0;
			break;
		case 31:
			override_b16 = 0;
			break;
		case 32:
			override_tbc_r = 0;
			break;
		case 33:
			override_tbc_g = 0;
			break;
		case 34:
			override_tbc_b = 0;
			break;
		case 35:
			if(*optarg == 'u' || *optarg == 'U'){override_r_sign = 0;}
			else if(*optarg == 's' || *optarg == 'S'){override_r_sign = 1;}
			else{override_r_sign = atoi(optarg);}
			break;
		case 36:
			if(*optarg == 'u' || *optarg == 'U'){override_g_sign = 0;}
			else if(*optarg == 's' || *optarg == 'S'){override_g_sign = 1;}
			else{override_g_sign = atoi(optarg);}
			break;
		case 37:
			if(*optarg == 'u' || *optarg == 'U'){override_b_sign = 0;}
			else if(*optarg == 's' || *optarg == 'S'){override_b_sign = 1;}
			else{override_b_sign = atoi(optarg);}
			break;
		default:
			usage();
			break;
		}
	}

	if (dev_index < 0)
	{
		fprintf(stderr, "\nDevice number invalid\n\n");
		usage();
	}
	
	if(read_mode < 0 || read_mode > 3)
	{
		fprintf(stderr, "\nRead mode unknown\n\n");
		usage();
	}

	if(red == 0 && green == 0 && blue == 0)
	{
		fprintf(stderr, "\nNo file provided using option (-R,-G,-B)\n\n");
		usage();
	}
	
	if((red == 0 && red2 == 1) || (green == 0 && green2 == 1) || (blue == 0 && blue2 == 1))
	{
		fprintf(stderr, "\nNo main file provided using (-R,-G,-B)\n\n");
		usage();
	}
	
	if((override_r_sign < -1 || override_r_sign > 1) || (override_g_sign < -1 || override_g_sign > 1) || (override_b_sign < -1 || override_b_sign > 1))
	{
		fprintf(stderr, "\nInvalid value for one of the option (-signR,-signG,-signB) value : (0,u,1,s)\n\n");
		usage();
	}
	
	if((sync_a != 'R') && (sync_a != 'G') && (sync_a != 'B'))
	{
		fprintf(stderr, "\nUnknow parametter '%c' for option -syncA / value : (R,r,G,g,B,b)\n\n",sync_a);
		usage();
	}
	else if(sync_a == 'R'){start_audio = start_r;}
	else if(sync_a == 'G'){start_audio = start_g;}
	else if(sync_a == 'B'){start_audio = start_b;}
	else if(red == 1 && green == 0 && blue == 0){start_audio = start_r; sync_a = 'R';}//select the channel if only 1 is activated
	else if(red == 0 && green == 1 && blue == 0){start_audio = start_g; sync_a = 'G';}
	else if(red == 0 && green == 0 && blue == 1){start_audio = start_b; sync_a = 'B';}
	else {start_audio = start_g; sync_a = 'G';}//default value
	
	if((pipe_mode != 'A') && (pipe_mode != 'R') && (pipe_mode != 'G') && (pipe_mode != 'B'))
	{
		fprintf(stderr, "\nUnknow parametter '%c' for option -pipeMode / value : (A,a,R,r,G,g,B,b)\n\n",pipe_mode);
		usage();
	}
	
	if((cmb_mode_r < 0 || cmb_mode_r > 1) || (cmb_mode_g < 0 ||cmb_mode_g > 1) || (cmb_mode_b < 0 || cmb_mode_b > 1))
	{
		fprintf(stderr, "\nCombine mode invalid / value : (0 ,1)\n\n");
		usage();
	}
	
	if((c_gain_r < 0 || c_gain_r > 6) || (c_gain_g < 0 || c_gain_g > 6) || (c_gain_b < 0 || c_gain_b > 6))
	{
		fprintf(stderr, "\nOne chroma gain is invalid / range : (0.0 ,4.0)\n\n");
		usage();
	}
	
	if((signal_gain_r < 0.5 || signal_gain_r > 2) || (signal_gain_g < 0.5 || signal_gain_g > 2) || (signal_gain_b < 0.5 || signal_gain_b > 2))
	{
		fprintf(stderr, "\nOne signal gain is invalid / range : (0.0 ,4.0)\n\n");
		usage();
	}
	
	if((ire_r < -50 || ire_r > 50) || (ire_g < -50 || ire_g > 50) || (ire_b < -50 || ire_b > 50))
	{
		fprintf(stderr, "\nIRE level is invalid / range : (-50.0 , 50.0)\n\n");
		usage();
	}
	
	if(samp_rate == 17734475 || samp_rate == 17735845)//PAL
	{
		start_r = start_r * ((709375 + (1135 * tbcR)) * (1 + r16));// set first frame
		start_g = start_g * ((709375 + (1135 * tbcB)) * (1 + g16));
		start_b = start_b * ((709375 + (1135 * tbcG)) * (1 + b16));
		start_audio = (start_audio + audio_offset) * ((88200/25) * 2);
		video_standard = 'P';
	}
	else if(samp_rate == 14318181 || samp_rate == 14318170)//NTSC
	{
		start_r = start_r * ((477750 + (910 * tbcR)) * (1 + r16));//set first frame
		start_g = start_g * ((477750 + (910 * tbcG)) * (1 + g16));
		start_b = start_b * ((477750 + (910 * tbcB)) * (1 + b16));
		start_audio = (start_audio + audio_offset) * ((88200/30) * 2);
		video_standard = 'N';
	}
	
//RED
if(red == 1)
{
	if (strcmp(filename_r, "-") == 0)/* Read samples from stdin */
	{
		file_r = stdin;
	}
	else
	{
		file_r = fopen(filename_r, "rb");
		if (!file_r) {
			fprintf(stderr, "(RED) : Failed to open %s\n", filename_r);
			return -ENOENT;
		}
		else
		{
			FSEEK(file_r,start_r,0);
		}
		
		if(!strcmp(get_filename_ext(filename_r),"tbc")){r16 = 1;r_sign = 0;tbcR = 1;}
		else if(!strcmp(get_filename_ext(filename_r),"s8")){r16 = 0;r_sign = 1;}
		else if(!strcmp(get_filename_ext(filename_r),"u8")){r16 = 0;r_sign = 0;}
		else if(!strcmp(get_filename_ext(filename_r),"s16")){r16 = 1;r_sign = 1;}
		else if(!strcmp(get_filename_ext(filename_r),"u16")){r16 = 1;r_sign = 0;}
		else if(!strcmp(get_filename_ext(filename_r),"wav")){r16 = 1;r_sign = 1;}
		else if(!strcmp(get_filename_ext(filename_r),"pcm")){r16 = 1;r_sign = 1;}
		else if(!strcmp(get_filename_ext(filename_r),"efm")){r16 = 0;r_sign = 0;}
		else if(!strcmp(get_filename_ext(filename_r),"raw")){r16 = 1;r_sign = 1;}
		else if(!strcmp(get_filename_ext(filename_r),"r8")){r16 = 0;r_sign = 0;}
		else if(!strcmp(get_filename_ext(filename_r),"r16")){r16 = 1;r_sign = 0;}
		//else if(!strcmp(get_filename_ext(filename_r),"cds")){r16 = 1;r_sign = 0;}//10 bit packed
	}

	txbuf_r = malloc(FL2K_BUF_LEN);
	if (!txbuf_r) {
		fprintf(stderr, "(RED) : malloc error!\n");
		goto out;
	}
}

if(red2 == 1)
{
	if (strcmp(filename2_r, "-") == 0)/* Read samples from stdin */
	{ 
		file2_r = stdin;
	}
	else 
	{
		file2_r = fopen(filename2_r, "rb");
		if (!file2_r) {
			fprintf(stderr, "(RED) : Failed to open %s\n", filename2_r);
			return -ENOENT;
		}
		else
		{
			FSEEK(file2_r,start_r,0);
		}
	}
}

//GREEN
if(green == 1)
{
	if (strcmp(filename_g, "-") == 0)/* Read samples from stdin */
	{
		file_g = stdin;
	}
	else
	{
		file_g = fopen(filename_g, "rb");
		if (!file_g) {
			fprintf(stderr, "(GREEN) : Failed to open %s\n", filename_g);
			return -ENOENT;
		}
		else
		{
			FSEEK(file_g,start_g,0);
		}
	}

	txbuf_g = malloc(FL2K_BUF_LEN);
	if (!txbuf_g) {
		fprintf(stderr, "(GREEN) : malloc error!\n");
		goto out;
	}
	
	if(!strcmp(get_filename_ext(filename_g),"tbc")){g16 = 1;g_sign = 0;tbcG = 1;}
	else if(!strcmp(get_filename_ext(filename_g),"s8")){g16 = 0;g_sign = 1;}
	else if(!strcmp(get_filename_ext(filename_g),"u8")){g16 = 0;g_sign = 0;}
	else if(!strcmp(get_filename_ext(filename_g),"s16")){g16 = 1;g_sign = 1;}
	else if(!strcmp(get_filename_ext(filename_g),"u16")){g16 = 1;g_sign = 0;}
	else if(!strcmp(get_filename_ext(filename_g),"wav")){g16 = 1;g_sign = 1;}
	else if(!strcmp(get_filename_ext(filename_g),"pcm")){g16 = 1;g_sign = 1;}
	else if(!strcmp(get_filename_ext(filename_g),"efm")){g16 = 0;g_sign = 0;}
	else if(!strcmp(get_filename_ext(filename_g),"raw")){g16 = 1;g_sign = 1;}
	else if(!strcmp(get_filename_ext(filename_g),"r8")){g16 = 0;g_sign = 0;}
	else if(!strcmp(get_filename_ext(filename_g),"r16")){g16 = 1;g_sign = 0;}
	//else if(!strcmp(get_filename_ext(filename_g),"cds")){g16 = 1;g_sign = 0;}
}

if(green2 == 1)
{
	if (strcmp(filename2_g, "-") == 0)/* Read samples from stdin */
	{ 
		file2_g = stdin;
	}
	else 
	{
		file2_g = fopen(filename2_g, "rb");
		if (!file2_g) {
			fprintf(stderr, "(GREEN) : Failed to open %s\n", filename2_g);
			return -ENOENT;
		}
		else
		{
			FSEEK(file2_g,start_g,0);
		}
	}
}

//BLUE
if(blue == 1)
{
	if (strcmp(filename_b, "-") == 0)/* Read samples from stdin */
	{
		file_b = stdin;
	}
	else
	{
		file_b = fopen(filename_b, "rb");
		if (!file_b) {
			fprintf(stderr, "(BLUE) : Failed to open %s\n", filename_b);
			return -ENOENT;
		}
		else
		{
			FSEEK(file_b,start_b,0);
		}
	}

	txbuf_b = malloc(FL2K_BUF_LEN);
	if (!txbuf_b) {
		fprintf(stderr, "(BLUE) : malloc error!\n");
		goto out;
	}
	
	if(!strcmp(get_filename_ext(filename_b),"tbc")){b16 = 1;b_sign = 0;tbcB = 1;}
	else if(!strcmp(get_filename_ext(filename_b),"s8")){b16 = 0;b_sign = 1;}
	else if(!strcmp(get_filename_ext(filename_b),"u8")){b16 = 0;b_sign = 0;}
	else if(!strcmp(get_filename_ext(filename_b),"s16")){b16 = 1;b_sign = 1;}
	else if(!strcmp(get_filename_ext(filename_b),"u16")){b16 = 1;b_sign = 0;}
	else if(!strcmp(get_filename_ext(filename_b),"wav")){b16 = 1;b_sign = 1;}
	else if(!strcmp(get_filename_ext(filename_b),"pcm")){b16 = 1;b_sign = 1;}
	else if(!strcmp(get_filename_ext(filename_b),"efm")){b16 = 0;b_sign = 0;}
	else if(!strcmp(get_filename_ext(filename_b),"raw")){b16 = 1;b_sign = 1;}
	else if(!strcmp(get_filename_ext(filename_b),"r8")){b16 = 0;b_sign = 0;}
	else if(!strcmp(get_filename_ext(filename_b),"r16")){b16 = 1;b_sign = 0;}
	//else if(!strcmp(get_filename_ext(filename_b),"cds")){b16 = 1;b_sign = 0;}
}

if(blue2 == 1)
{
	if (strcmp(filename2_b, "-") == 0)/* Read samples from stdin */
	{ 
		file2_b = stdin;
	}
	else 
	{
		file2_b = fopen(filename2_b, "rb");
		if (!file2_b) {
			fprintf(stderr, "(BLUE) : Failed to open %s\n", filename2_b);
			return -ENOENT;
		}
		else
		{
			FSEEK(file2_b,start_b,0);
		}
	}
}

if(audio == 1)
{
	if (strcmp(filename_audio, "-") == 0)/* Read samples from stdin */
	{ 
		file_audio = stdin;
	}
	else 
	{
		file_audio = fopen(filename_audio, "rb");
		if (!file_audio) {
			fprintf(stderr, "(AUDIO) : Failed to open %s\n", filename_audio);
			return -ENOENT;
		}
		else
		{
			FSEEK(file_audio,start_audio,0);
		}
	}
}

//16bit override
if(override_r16 != -1){r16 = override_r16;}
if(override_g16 != -1){g16 = override_g16;}
if(override_b16 != -1){b16 = override_b16;}

//tbc override
if(override_tbc_r != -1){tbcR = override_tbc_r;}
if(override_tbc_g != -1){tbcG = override_tbc_g;}
if(override_tbc_b != -1){tbcB = override_tbc_b;}

//sign override
if(override_r_sign != -1){r_sign = override_r_sign;}
if(override_g_sign != -1){g_sign = override_g_sign;}
if(override_b_sign != -1){b_sign = override_b_sign;}

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
	
	if(red2 == 1)
	{
		if (file2_r && (file2_r != stdin))
		{
			fclose(file2_r);
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
	
	if(green2 == 1)
	{
		if (file2_g && (file2_g != stdin))
		{
			fclose(file2_g);
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
	
	if(blue2 == 1)
	{
		if (file2_b && (file2_b != stdin))
		{
			fclose(file2_b);
		}
	}
	
	if(audio == 1)
	{
		if (file_audio && (file_audio != stdin))
		{
			fclose(file_audio);
		}
	}
	
	//free(pipe_buf);

	return 0;
}
