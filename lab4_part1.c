#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <mraa/aio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SIZE 256

sig_atomic_t volatile run_flag = 1;

const int B=4275;                 // B value of the thermistor
const int R0 = 100000;            // R0 = 100k
int log_fd;

void do_when_interrupted(int sig)
{
	if(sig == SIGINT)
		run_flag = 0;
}

int main()
{
	log_fd = open("log_part1.txt", O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU);
	mraa_aio_context temp_sensor;
	temp_sensor = mraa_aio_init(0);

	signal(SIGINT, do_when_interrupted);
	
	time_t curr_time;
	char buffer[SIZE];

	while(run_flag){
		float a = mraa_aio_read(temp_sensor);

       	 	float R = 1023.0/((float)a)-1.0;
        	R = 100000.0*R;
        	float temperature=1.0/(log(R/100000.0)/B+1/298.15)-273.15;//convert to temperature via datasheet ;
		temperature = temperature * (9/5);
		temperature += 32;
		struct tm *loctime;
		curr_time = time(NULL);
		loctime = localtime(&curr_time);
		strftime(buffer, SIZE, "%T", loctime);
		
		printf("%s ", buffer);
		printf("%.1f\n", temperature);
		dprintf(log_fd, "%s ", buffer);
		dprintf(log_fd, "%.1f\n", temperature);
	
        	sleep(1);
	
	}
	mraa_aio_close(temp_sensor);
	return 0;
}
