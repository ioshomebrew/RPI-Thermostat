// TODO 
// 1) Implement dht22 driver Check
// 2) Implement config system Check
// 3) Implement GPIO events when temperature reaches set points Check
// 4) Implement command line interface Check
// 5) Implement web interface Partial Javascript all that's left

#include "dht22.h"
#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "locking.h"

// libmicrohttpd stuff
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <microhttpd.h>
#define PORT 8888

#define MAXBYTES 80

// load html file
char *loadHTML(char *filename)
{
	FILE *file = fopen(filename, "rb");
	if(file == NULL)
	{
		printf("File couldn't be opened\n");
		return NULL;
	}
	int prev = ftell(file);
	fseek(file, 0L, SEEK_END);
	long int size = ftell(file);
	fseek(file, prev, SEEK_SET);

	char *data = malloc(sizeof(char)*size);
	if(data == NULL)
	{
		printf("Memory alloc error\n");
		return NULL;
	}

  	fread(data, 1, size, file);

	return data;
}

// connection answer function
int answer_to_connection(void *cls, struct MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls)
{
	const char *page = loadHTML("main.html");

	struct MHD_Response *response;
	int ret;

	response = MHD_create_response_from_buffer(strlen(page), (void*)page, MHD_RESPMEM_PERSISTENT);

	ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
	MHD_destroy_response (response);

	return ret;
}

// enum for hvac mode
enum hvac
{
	AC, HEAT, OFF
};

// enum for fan mode
enum fan
{
	ON, AUTO
};

// LED Yellow
void blowerOn()
{
	// PIN  13/GPIO 27
	digitalWrite(27, HIGH);
}

// LED Yellow
void blowerOff()
{
	// PIN 13/GPIO 27
	digitalWrite(27, LOW);
}

// LED Green
void ACOn()
{
	// PIN 11/GPIO 17
	digitalWrite(17, HIGH);
}

// LED Green
void ACoff()
{
	// PIN 11/GPIO 17
	digitalWrite(17, LOW);
}

// LED Red
void HeatOn()
{
	// PIN 15/GPIO 22
	digitalWrite(22, HIGH);
}

// LED Red
void HeatOff()
{
	// PIN 15/GPIO 22
	digitalWrite(22, LOW);
}

// default settings
void defaultSettings(FILE *p)
{
	fprintf(p, "hvacMode = 2\n");
	fprintf(p, "fanMode = 1\n");
	fprintf(p, "heatTemp = 74.00\n");
	fprintf(p, "coolTemp = 70.00\n");
	fprintf(p, "offsetVal = 0.0\n");
}

// print command line help
void printHelp()
{
	printf("Valid command options\n");
    	printf("Note: commands are case sensitive\n");
	printf("h: help\n");
	printf("sht = XX.XX: set high temp\n");
	printf("slt = XX.XX: set low temp\n");
	printf("sov = XX.XX: set offset value\n");
	printf("shm = AC/HEAT/OFF: set hvac mode\n");
	printf("sfm = AUTO/ON: set blower mode\n");
	printf("ps: print settings\n");
	printf("p: print temp\n");
    	printf("s: save settings\n");
	printf("q: quit\n");
}

// main loop
int main()
{
	int lockfd;
	int tries = 100;

	// libmicrohttpd daemon
	struct MHD_Daemon *daemon;

	daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL, &answer_to_connection, NULL, MHD_OPTION_END);
	if(daemon == NULL)
	{
		printf("Error initializing webserver\n");
		return 1;
	}

	// timer counter for reading
	struct timespec lastReset, currentTime;

	// config data
	int hvacReady = 0;
	int hvacOn = 0;
	int sensorReady = 0;
	int hvacMode = AC;
	int fanMode = ON;
	float heatTemp = 0.0;
	float coolTemp = 0.0;
	float offsetVal = 0.0;

	// data from am2302
	float temperature;
	float humidity;

	// select data
	int fd_stdin;
	fd_set readfds;
	struct timeval tv;
	int num_readable;
	int num_bytes;
	char buf[MAXBYTES];
	char *command = malloc(sizeof(char)*MAXBYTES);

	fd_stdin = fileno(stdin);

	printf("RPIThermostat v1.0\n");
	printf("Copyright 2017 ioshomebrew\n");
	printf("Note: AM2302 sensor takes 5 min to correctly read temperature\n");

	// load config file if exists
	FILE *config = fopen("config.ini", "r");
	if(config)
	{
		// Read config
		char *line = malloc(15*sizeof(char));
		char equal;

		fscanf(config, "%s %c %i", line, &equal, &hvacMode);
		fscanf(config, "%s %c %i", line, &equal, &fanMode);
		fscanf(config, "%s %c %f", line, &equal, &heatTemp);
		fscanf(config, "%s %c %f", line, &equal, &coolTemp);
		fscanf(config, "%s %c %f", line, &equal, &offsetVal);
		free(line);

		// Print read settings
		printf("Settings are: \n");
		switch(hvacMode)
		{
			case AC:
			{
				printf("AC On\n");
			}
			break;
			
			case HEAT:
			{
				printf("Heat on\n");
			}
			break;

			case OFF:
			{
				printf("HVAC Off\n");
			}
			break;
		}

		switch(fanMode)
		{
			case ON:
			{
				printf("Fan On\n");
			}
			break;

			case AUTO:
			{
				printf("Auto Fan\n");
			}
			break;
		}

		printf("Heat temp is: %.2f\n", heatTemp);
		printf("Cool temp is: %.2f\n", coolTemp);		
		printf("Offset Val is: %.2f\n", offsetVal);
		puts("");
	}
	else
	{
		// Create config
		printf("config.ini not found, loading default settings, and creating config file\n");
		config = fopen("config.ini", "w");
		defaultSettings(config);
		fclose(config);
	}

	// open lockfile
	lockfd = open_lockfile(LOCKFILE);

	// wiringPi Init
	if(wiringPiSetup() == -1)
	{
		printf("WiringPi error\n");
		return -1;
	}
	wiringPiSetupGpio();

	// setup HVAC Output
	pinMode(17, OUTPUT);
	pinMode(27, OUTPUT);
	pinMode(22, OUTPUT);

	// reset HVAC system
	blowerOff();
	ACoff();
	HeatOff();

	// make sure sudo access works
	if(setuid(getuid()) < 0)
	{
		printf("Dropping privileges failed\n");
		return -1;
	}

	// print help menu
	printHelp();
	
	// get current time for lastReset
	clock_gettime(CLOCK_REALTIME, &lastReset);

	// main loop
	printf("-> ");
	while(1)
	{
		// get current time
		clock_gettime(CLOCK_REALTIME, &currentTime);

		// get data from temp sensor
		if((currentTime.tv_sec) - (lastReset.tv_sec) >= 3)
		{
			clock_gettime(CLOCK_REALTIME, &lastReset);
			if(read_dht22_dat(&temperature, &humidity))
			{
				sensorReady = 1;
			}
		}

		// check if program has run long enough to activate HVAC
		if(clock()/CLOCKS_PER_SEC >= 1)
		{
			if(!hvacReady)
			{
				printf("HVAC Ready\n");
				hvacReady = 1;
			}
	
			// see if AC, Heater, or Blower need to be activated
			switch(fanMode)
			{
				case ON:
				{
					blowerOn();
				}
				break;

				case AUTO:
				{
					if(hvacOn)
					{
						blowerOn();
					}
					else
					{
						blowerOff();
					}
				}
				break;
			}

			switch(hvacMode)
			{
				case HEAT:
				{
					if(CtoF(temperature)+offsetVal < heatTemp)
					{
						// turn heat on
						hvacOn = 1;
						HeatOn();
					}
					else if(CtoF(temperature)+offsetVal > heatTemp)
					{
						// turn heat off
						hvacOn = 0;
						HeatOff();
					}
				}
				break;

				case AC:
				{
					if(CtoF(temperature)+offsetVal > coolTemp)
					{
						// turn ac on
						hvacOn = 1;
						ACOn();
					}
					else if(CtoF(temperature)+offsetVal < coolTemp)
					{
						// turn ac off
						hvacOn = 0;
						ACoff();
					}
				}
				break;
				
				case OFF:
				{
					// make sure ac and heat are off
					hvacOn = 0;
					ACoff();
					HeatOff();
				}
				break;
			}
		}

		// check for input
		FD_ZERO(&readfds);
		FD_SET(fileno(stdin), &readfds);

		// pause every 1ms to check for input
		tv.tv_sec = 0;
		tv.tv_usec = 1;

		fflush(stdout);
		num_readable = select(fd_stdin + 1, &readfds, NULL, NULL, &tv);
		if (num_readable == -1)
		{
			// if error in select function
			printf("error in select\n");
		}
		if (num_readable == 0)
		{
			// if no data is entered
		}
		else
		{
			// get data user entered
			char equal;
			num_bytes = read(fd_stdin, buf, MAXBYTES);
			sscanf(buf, "%s", command);
		
			// new line
			puts("");
	
			// process command
			if(strcmp(command, "p") == 0)
			{
               			// print current temperature
				if(sensorReady)
				{
					printf("Current temp is: %.2f\n", CtoF(temperature)+offsetVal);
				}
				else
				{
					printf("Sensor not ready\n");
				}
			}
			else if(strcmp(command, "h") == 0)
			{
                		// print help menu
				printHelp();
			}
			else if(strcmp(command, "q") == 0)
			{
		                // print quit menu
				printf("Quiting now\n");
				break;
			}
            		else if(strcmp(command, "s") == 0)
            		{
                		// save settings
                		config = fopen("config.ini", "w");
				if(config)
				{
                			fprintf(config, "hvacMode = %i\n", hvacMode);
                			fprintf(config, "fanMode = %i\n", fanMode);
                			fprintf(config, "heatTemp = %.2f\n", heatTemp);
                			fprintf(config, "coolTemp = %.2f\n", coolTemp);
                			fprintf(config, "offsetVal = %.2f\n", offsetVal);
                			fclose(config);
					printf("Settings saved\n");
				}
				else
				{
					printf("Error writing settings\n");
				}
            		}
			else if(strcmp(command, "sht") == 0)
			{
				// set high temperature
				sscanf(buf, "%s %c %f", command, &equal, &heatTemp);
				printf("New high temp is: %.2f\n", heatTemp);
			} 
			else if(strcmp(command, "slt") == 0)
			{
				// set low temperature
				sscanf(buf, "%s %c %f", command, &equal, &coolTemp);
				printf("New low temp is: %.2f\n", coolTemp);
			}
			else if(strcmp(command, "sov") == 0)
			{
				// set offset value
				sscanf(buf, "%s %c %f", command, &equal, &offsetVal);
				printf("New offset value is: %.2f\n", offsetVal);
			}
			else if(strcmp(command, "shm") == 0)
			{
				// set hvac mode

				// reset HVAC
				blowerOff();
				ACoff();
				HeatOff();				

				char *mode = malloc(sizeof(char)*10);
				sscanf(buf, "%s %c %s", command, &equal, mode);
				if(strcmp(mode, "AC") == 0)
				{
					// setting hvac mode to AC
					hvacMode = AC;
					printf("hvacMode is now AC\n");
				}
				else if(strcmp(mode, "HEAT") == 0)
				{
					// setting HVAC mode to heat
					hvacMode = HEAT;
					printf("hvacMode is now HEAT\n");
				}
				else if(strcmp(mode, "OFF") == 0)
				{
					// setting HVAC mode to off
					hvacMode = OFF;
					printf("hvacMode is now OFF\n");
				}
				else
				{
					printf("Invalid mode set\n");
				}
				free(mode);
			}
			else if(strcmp(command, "sfm") == 0)
			{
				// set blower mode
				char *mode = malloc(sizeof(char)*10);
				sscanf(buf, "%s %c %s", command, &equal, mode);
				
				if(strcmp(mode, "ON") == 0)
				{
					fanMode = ON;
					printf("fanMode is now ON\n");
				}
				else if(strcmp(mode, "AUTO") == 0)
				{
					fanMode = AUTO;
					printf("fanMode is now AUTO\n");
				}
				else
				{
					printf("Invalid mode set\n");
				}
				free(mode);
			}
			else if(strcmp(command, "ps") == 0)
			{
                		printf("Settings are: \n");
                		switch(hvacMode)
                		{
                    			case AC:
                    			{
                        			printf("AC On\n");
                    			}
                        		break;
                        
                    			case HEAT:
                    			{
                        			printf("Heat on\n");
                    			}
                        		break;
                        
                    			case OFF:
                    			{
                        			printf("HVAC Off\n");
                    			}
                        		break;
                		}
                
                		switch(fanMode)
                		{
                    			case ON:
                    			{
                        			printf("Fan On\n");
                    			}
                        		break;
                        
                   			case AUTO:
                    			{
                        			printf("Auto Fan\n");
                    			}
                        		break;
                		}
                
                		printf("Heat temp is: %.2f\n", heatTemp);
                		printf("Cool temp is: %.2f\n", coolTemp);
                		printf("Offset Val is: %.2f\n", offsetVal);
            		}
			else
			{
				printf("Invalid command\n");
			}

			printf("-> ");
		}
	}

	// turn HVAC system off
	ACoff();
	HeatOff();
	blowerOff();

	delay(1500);
	MHD_stop_daemon(daemon);
	close_lockfile(lockfd);

	return 0;
}
