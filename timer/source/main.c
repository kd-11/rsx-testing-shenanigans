#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>

#include <sys/systime.h>

u64 get_time_us(u64 secs, u64 nsecs)
{
	return (secs * 1000000) + (nsecs / 1000);
}

u64 measure_usleep(u64 timeout, int sleep)
{
	u64 sec, nsec, sec2, nsec2;
	u64 t = 0;
	int loop;
	
	for (loop = 0; loop < 1000; ++loop)
	{
		sysGetCurrentTime(&sec, &nsec);
		if (sleep) sysUsleep(timeout);
		sysGetCurrentTime(&sec2, &nsec2);
		t += (get_time_us(sec2, nsec2) - get_time_us(sec, nsec));
	}
	
	return t / 1000;
}

void do_run(u64 timeout, u64 baseline)
{
	printf("Testing usleep(%llu)...\n", timeout);
	u64 t = measure_usleep(timeout, 1);
	
	if (t >= baseline) t -= baseline;
	else t = 0;
	
	printf("    Latency = %lluus\n", t);
}

void run_test()
{
	printf("Calculating baseline delay...");
	u64 baseline = measure_usleep(0, 0);
	printf("(%lluus)\n", baseline);
	
	do_run(0, baseline);
	do_run(1, baseline);
	do_run(10, baseline);
	do_run(50, baseline);
	do_run(100, baseline);
	do_run(200, baseline);
	do_run(300, baseline);
	do_run(400, baseline);
	do_run(500, baseline);
	
	printf("Doing the special tests...\n");
	do_run(250, baseline);
	do_run(280, baseline);
	do_run(290, baseline);
	do_run(299, baseline);
	do_run(301, baseline);
	do_run(310, baseline);
	do_run(320, baseline);
	do_run(600, baseline);
}

int main(int argc, char** argv)
{
	printf("Application started\n");
	run_test();
	printf("Application finished\n");
	return 0;
}