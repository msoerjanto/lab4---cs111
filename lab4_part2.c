#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/wait.h>
#include <strings.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <mraa/aio.h>

#define BUFSIZE 256

//thread to wait for initial server response
pthread_t m_thread;
//thread to get commands from server
pthread_t command_thread;
//lock, used when updating global variables
pthread_mutex_t update_lock;

//global variables for connecting to server
struct hostent * server;
struct sockaddr_in serv_addr;

//file descriptors to refer to files
int sockfd;
int log_fd;

//value of thermistor
const int B = 4275;

//port number
int portno = 16000;

//global buffer
char *buffer;
//int buffer to get new port number
int port_buffer;
//buffer to store latest command received
char command_buffer[10];
//temporary buffer to store argument of SCALE command
char scale_arg = 'F';
//buffer to store argument of FREQ command
int freq_arg;
//buffer for reading from server
char *read_buffer;
//constant c-string to hold uid and Port request string
char uid[] = {'2','0','4','4','9','7','1','9','9'};
char cons_str[] = {'P', 'o', 'r', 't', ' ', 'r', 'e', 'q', 'u', 'e', 's', 't', ' '};

//flag to indicate that a command is invalid
int invalid_flag = 0;
//this value determines which command to handle
int command_flag = 0;
//flag for STOP and START commands
sig_atomic_t volatile run_flag = 1;
//flag for FREQ intially 3
int m_freq = 3;
//flag for SCALE 
char m_scale = 'F';
//flag for OFF
int on_flag = 1;

//function to be executed by thread, waits for input from server and sets the port number accordingly
void *receive_new_port()
{
	int nchar = read(sockfd, &port_buffer, sizeof(int));
	while(nchar)
	{
		if(nchar <= 0)
		{
			perror("read error");
			exit(0);
		}else
		{
			portno = port_buffer;		
		}
		memset(read_buffer, 0, nchar);
		nchar = read(sockfd, read_buffer, BUFSIZE);
	}	       
}

//handles the command based on command_flag, updates global variables corresponding to commands
void handle_command()
{
	switch(command_flag)
	{
	case 0://OFF
		on_flag = 0;
		break;
	case 1://STOP
		run_flag = 0;
		break;
	case 2://START
		run_flag = 1;
		break;
	case 3://SCALE command
		m_scale = scale_arg;		
		break;
	case 4: //FREQ command
		m_freq = freq_arg;
		break;
	}
}

//thread executed function, receives and handles commands from server
void *receive_command()
{
	buffer = (char*)malloc(sizeof(char) * BUFSIZE);
	int nchar = read(sockfd, buffer, BUFSIZE);
	while(nchar)
	{
		if(nchar <= 0)
		{
			perror("read error");
			exit(0);
		}else
		{
			//all valid commands below 10 characters
			if(nchar <= 9)
			{
				//handles OFF STOP START
				if(nchar <= 5)
				{
					if(strcmp(buffer, "OFF") == 0 ||
					   strcmp(buffer, "STOP") == 0 ||
					   strcmp(buffer, "START") == 0)
					{
						strcpy(command_buffer, buffer);
						invalid_flag = 0;
						if(strcmp(buffer, "OFF") == 0)
							command_flag = 0;
						else if(strcmp(buffer, "STOP") == 0)
							command_flag = 1;
						else
							command_flag = 2;
					}else
					{
						invalid_flag = 1;
					}
				}
				else if(nchar < 10)
				{
					//handles FREQ and SCALE
					char *temp;
					if(buffer[5] == '=')
					{
						temp = (char*)malloc(sizeof(char)*6);
						int i;
						for(i = 0; i < 5; i++)
						{
							temp[i] = buffer[i];
						}
						temp[5] = '\0';
						if(strcmp(temp, "SCALE") == 0)
						{
							if(buffer[6] == 'F')
							{
								strcpy(command_buffer,"SCALE");
								scale_arg = 'F';
								invalid_flag = 0;
								command_flag = 3;
							}else if(buffer[6] == 'C')
							{
								strcpy(command_buffer, "SCALE");
								scale_arg = 'C';
								invalid_flag = 0;
								command_flag = 3;
							}else
								invalid_flag = 1;
						}
					}
					else if(buffer[4] == '=')
					{
						temp = (char*)malloc(sizeof(char)*5);
						int j;
						for(j = 0; j < 4; j++)
						{
							temp[j] = buffer[j];
						}	
						temp[4] = '\0';
						if(strcmp(temp, "FREQ") == 0)
						{
							char *temp_arg = (char*)malloc(sizeof(char) * (nchar - 5));
							int digit_flag = 1;
							int k,l = 0;
							for(k = 5; buffer[k] != '\0'; k++)
							{
								if(!isdigit(buffer[k]))
								{
									digit_flag = 0;
									invalid_flag = 1;
								}else
								{	
									temp_arg[l] = buffer[k];
									l++;
								}	
							}

							if(digit_flag)
							{
								strcpy(command_buffer, "FREQ");
								freq_arg = atoi(temp_arg);
								invalid_flag = 0;
								command_flag = 4;
							}
						}
					}else
						invalid_flag = 1;

				}	
			}	
		
		}
		
		//prints out commands to log
		pthread_mutex_lock(&update_lock);
		if(invalid_flag)
			dprintf(log_fd, "%s I\n", buffer);
		else
		{
			dprintf(log_fd, "%s\n", buffer);
			handle_command();
		}
		pthread_mutex_unlock(&update_lock);

		memset(buffer, 0, BUFSIZE);
		nchar = read(sockfd, buffer,BUFSIZE);	
	}	
}

int connect_to_new_port()
{
	//close old socket
	close(sockfd);
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0)
	{
		perror("socket error");
		exit(0);
	}
	server = gethostbyname("lever.cs.ucla.edu");
	if(server == NULL)
	{
		printf("error connecting to host");
		exit(0);
	}
	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *) server->h_addr,
			(char *) &serv_addr.sin_addr.s_addr,
			server->h_length);
	serv_addr.sin_port = htons(portno);
	return connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
}

//initialize socket to server
int createSocket(int new)
{
	if(new)
		close(sockfd);
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0)
	{
		perror("socket error");
		exit(0);
	}
	server = gethostbyname("lever.cs.ucla.edu");
	if(server == NULL)
	{
		printf("error connecting to host");
		exit(0);
	}
	bzero((char*)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr,
		(char *)&serv_addr.sin_addr.s_addr,
		server->h_length);
	serv_addr.sin_port = htons(portno);
	return connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
}

//assigns id to buffer for writing
int assignIdToBuffer()
{
	int len_cons = sizeof(cons_str)/sizeof(char);
	int len_uid = sizeof(uid)/sizeof(char);
	int length = 0;
	memset(buffer, 0, BUFSIZE);

	int i;
	for(i = 0; i < len_cons; i++)
	{
		buffer[i] = cons_str[i];
		length++;
	}
	int j;
	for(j = 0; j < len_uid; j++, i++)
	{
		buffer[i] = uid[j];
	       length++;	
	}
	return length;
}

int main()
{
	//open log.txt for writing and initialize buffers
	log_fd = open("log_part2.txt", O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
	buffer = (char *)malloc(sizeof(char) * BUFSIZE);
	read_buffer = (char *)malloc(sizeof(char) * BUFSIZE);

	//attempt connection to server
	if(createSocket(0) < 0)
	{
		perror("error connecting");
		exit(0);
	}
	
	//send port request to server
	int length = assignIdToBuffer();
	//printf("%s\n", buffer);	
	write(sockfd, buffer, length);
	
	//make thread to get reply, wait for response before proceeding
	pthread_create(&m_thread, NULL,receive_new_port , NULL);
	pthread_join(m_thread, NULL);
	
	//attempt to connect to server with different port number
	if(createSocket(1) < 0)
	{
		perror("error connecting");
		exit(0);
	}else
	{
		//create new thread to receive commands
		pthread_create(&command_thread, NULL,receive_command, NULL);	
	}
	
	//initialize context 
	mraa_aio_context temp_sensor;
	//set to corresponding slot to correspond to physical sensor
	temp_sensor = mraa_aio_init(0);
	//initialize time and buffer variables
	time_t curr_time;
	char m_buffer[BUFSIZE];
	//handles all temperature outputs to server and log
	//behavior of loop dictated by various global variables corresponding to the different commands
	while(on_flag)
	{
		if(run_flag)
		{
			float a = mraa_aio_read(temp_sensor);
			float R = 1023.0/((float)a)-1.0;
			R = 100000.0 * R;
			float temperature = 1.0/(log(R/100000.0)/B + 1/298.15) - 273.15;
		
			if(scale_arg == 'F')
			{
				temperature = temperature * (9/5);
				temperature += 32;
			}
			struct tm *loctime;
			curr_time = time(NULL);
			loctime = localtime(&curr_time);
			strftime(m_buffer, BUFSIZE, "%T", loctime);
		
			//send message to server
			dprintf(sockfd, "%s ", uid);
			dprintf(sockfd, "TEMP=%.1f\n", temperature);
			//for testing		
			//dprintf(STDOUT_FILENO, "%s ", uid);
			//dprintf(STDOUT_FILENO, "TEMP=%.1f %c\n", temperature,m_scale);
			//send message to log
			dprintf(log_fd,"%s ", m_buffer);
	       		dprintf(log_fd, "%.1f %c\n", temperature, m_scale);
			sleep(m_freq);
		}
	}
	//pthread_join(command_thread, NULL);
	exit(0);

}
