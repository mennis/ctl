/*
 * Copyright (c) 2015, Coraid, Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Coraid nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL CORAID BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _FILE_OFFSET_BITS 64
//#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>


typedef unsigned long ulong;
#define MBSIZE (512*17*512)
#define BUCKET_SIZE 10
#define UPDATE_RATE 0.25

char *mb;
unsigned long long sec;
unsigned long long key;
int rflag;
int wflag;
int aflag;
int tflag;
int qflag;  /* quiet */
double bucket[BUCKET_SIZE];
int curbucket;
off_t total;
off_t skip;

/* consolidate usage */
void
usage(char *argv0)
{
	fprintf(stderr, "usage: %s [-rwqh?ta] [-T TOTAL] [-s SKIP] [-k KEY] file\n", argv0);
}

void
setmb(void)
{
	unsigned long long *u, *ue, t;
	char *c, *ce;

	c = mb;
	ce = mb + MBSIZE;
	for (; c<ce; c+=512) {
		u = (unsigned long long *)c;
		ue = u + 512/sizeof *u;
		for (t = 0; u<ue; u++, t++)
			*u = (t & 1) ? sec : key;
		sec++;
	}
}

double 
getsecs(void)
{
	struct timespec tv;
	double current;

	clock_gettime(CLOCK_MONOTONIC, &tv);
	current = tv.tv_sec + 1.0e-09 * tv.tv_nsec;

	return current;
}

double 
calcrate(double cursecs)
{
	double start, end, rate;
	bucket[curbucket % BUCKET_SIZE] = cursecs;
	start = bucket[(curbucket + 1) % BUCKET_SIZE];
	end = bucket[curbucket % BUCKET_SIZE];
	
	rate = (((double)MBSIZE * (BUCKET_SIZE - 1)) / (double)(1024*1024)) / (end - start);
	curbucket++;
	return rate;
}

double
calcfinalrate(double start, int num)
{
	double end = bucket[num % BUCKET_SIZE];
	double rate = (((double)MBSIZE * num) / (1024*1024)) / (end - start);
	return rate;
}

void
dowrite(int fd)
{
	int n;
	double startsec = getsecs();
	double cursec = getsecs();
	off_t off, toll;

	curbucket = 0;
	toll = 0;
	sec = skip / 512;
	if (total == 0) {
		off = lseek(fd, 0, SEEK_END);
		if (off < 0) {
			fprintf(stderr,"unable to seek to end of file");
			exit(-1);
		}
		if (off < skip) {
			fprintf(stderr, "Skip %ld greater than file length %ld", skip, off);
			exit(-1);
		}
		off -= skip;
	} else
		off = total;
	if (qflag == 0)
		printf("beginning pattern write at sector %llu for %ld sectors\n", sec, off / 512);
	if (lseek(fd, skip, SEEK_SET) < 0) {
		fprintf(stderr,"unable to seek in file");
		exit(-1);
	}
loop:
	setmb();
	n = MBSIZE;
	if (total) {
		if (total - toll < n)
			n = total - toll;
		toll += n;
	}
	n = write(fd, mb, (size_t) n);
	if (tflag) {
		if ((getsecs() - cursec) >= UPDATE_RATE) {
			cursec = getsecs();	
			printf("Write rate: %5.1f MiB/s   \r", 
				calcrate(cursec));
			fflush(stdout);
		} else
			calcrate(getsecs());
	}
	if (n <= 0 || (total && toll >= total)) {
		if (n < 0)
		if (errno != ENOSPC) { /* We expect to fill the device */
			fprintf(stderr, "write error: %s\n",strerror(errno));
			exit(errno);
		}
		if (qflag == 0) 
			printf("write complete\n");
		if (tflag)
			printf("Avg write rate: %5.1f MiB/s    \n",
				calcfinalrate(startsec, curbucket));
		return;
	}
	goto loop;
}

void
doverify(int fd)
{
	int n;
	unsigned long long s, *u, *ue, t, ss;
	char *c, *ce;
	off_t off;
	double startsec = getsecs();
	double cursec = getsecs();

	if (total == 0) {
		off = lseek(fd, 0, SEEK_END);
		if (off < 0) {
			fprintf(stderr,"unable to seek to end of file");
			exit(-1);
		}
		if (off < skip) {
			fprintf(stderr, "Skip %ld greater than file length %ld", skip, off);
			exit(-1);
		}
		off -= skip;
	} else
		off = total;
	ss = skip / 512;
	sec = off / 512;
	if (lseek(fd, skip, SEEK_SET) < 0) {
		fprintf(stderr,"unable to seek in file");
		exit(-1);
	}
	if (qflag == 0) 
		printf("begin data verification at sector %llu for %llu sectors\n", ss, sec);

	c = ce = NULL;
	curbucket = 0;
	for (s=ss; s<sec + ss; s++) {
		if (c >= ce) {
			n = read(fd, mb, (size_t)MBSIZE);
			if (n <= 0) {
				fprintf(stderr, "read error @ sector %llu, error %d\n", s,n);
				break;
			}
			if (tflag) {
				if ((getsecs() - cursec) >= UPDATE_RATE) {
					cursec = getsecs();	
					printf("Read rate: %5.1f MiB/s     \r", 
						calcrate(getsecs()));
					fflush(stdout);
				} else
					calcrate(getsecs());
			}
			c = mb;
			ce = mb + n;
		}
		u = (unsigned long long *)c;
		ue = u + 512/sizeof *u;
		for (t = 0; u<ue; u++, t++) {
			if (t & 1) {
				if (*u != s) {
					fprintf(stderr,"data mismatch error at offset %llu\n", s);
					if (!aflag) {
						fprintf(stderr,"Failed.\n");
						exit(-5);
					}
					break;
				}
			} else {
				if (*u != key) {
					fprintf(stderr,"key mismatch error %llu != %llu at offset %llu\n",*u, key, s);
					if (!aflag) {
						fprintf(stderr,"Failed.\n");
						exit(-6);
					}
					break;
				}
			}
		}
		c += 512;
	}
	if (tflag)
		printf("Avg read rate: %3.1f MiB/s  \n", 
			calcfinalrate(startsec, curbucket));
	if (qflag == 0) 
		printf("verify complete\n");
}

unsigned long long
fstrtoll(char *str)
{
	double d;
	char *p;

	d = strtod(str, &p);
	if (d < 0 || p == str)
		goto error;
	switch (*p) {
	case 'g':
	case 'G':
		d *= 1000;
	case 'm':
	case 'M':
		d *= 1000;
	case 'K':
	case 'k':
		d *= 1000;
		p++;
	}
	if (*p != '\0') {
error:		fprintf(stderr, "invalid number format %s\n", str);
		exit(1);
	}
	return (unsigned long long)d;
}

int
main(int argc, char *argv[])
{
	int fd;
	int ch;

	if (posix_memalign((void**)&mb,sysconf(_SC_PAGESIZE),MBSIZE) != 0) {
		fprintf(stderr, "error posix_memalign failure\n");
		exit(-7);
	}

	while ((ch = getopt(argc, argv, "rwatq?s:k:T:")) != -1)
		switch (ch) {
			case 'r':
				rflag++;
				break;
			case 'w':
				wflag++;
				break;
			case 'a':
				aflag++;
				break;
			case 't':
				tflag++;
				break;
			case 'k':
				key = strtoull(optarg, 0, 0);
				break;
			case 'q':
				qflag++;
				break;
			case 'T':
				total = fstrtoll(optarg);
				total &= ~0x1ff;
				break;
			case 's':
				skip = fstrtoll(optarg);
				skip &= ~0x1ff; 
				break;
			case '?':
		                usage(argv[0]);
				printf("\t-r -- read (verify mode)\n"
				"\t-w -- write (initialize mode)\n"
				"\t-q -- quiet\n"
				"\t-? -- show this help\n"
				"\t-t -- show read/write rate\n"
				"\t-a -- do not abort on errors\n"
				"\t-T TOTAL -- total size to write\n"
				"\t-s SKIP -- skip SKIP sectors\n"
				"\t-k KEY -- write KEY (number) along with sector information.\n"
				"\t   Use same key for write and read.\n"
				"\t   Defaults to 0 if -k switch not present.\n"
        "\t   unsigned long long integer - may be in hex, e.g. 0xDEADB10C"
				"\tfile -- block device to test\n");
				exit(1);
			default:
				fprintf(stderr, "flag %c unknown\n", ch);
				usage(argv[0]);
				exit(1);
		}


	if (rflag && wflag)
		rflag = wflag = 0;
	if (argc <= 1) {
		usage(argv[0]);
		return -1;
	}

	fd = open(argv[optind], O_DIRECT|O_RDWR|O_CREAT|O_TRUNC, 0777);
	if (fd == -1) {
		fprintf(stderr,"error cannot open %s: %s", argv[optind],strerror(errno));
		exit(-1);
	}
	if (!rflag)
		dowrite(fd);
	if (!wflag)
		doverify(fd);
	return 0;
}

