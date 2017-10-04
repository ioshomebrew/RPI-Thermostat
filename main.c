// TODO 
// 1) Implement dht22 driver Check
// 2) Implement config system Check
// 3) Implement GPIO events when temperature reaches set points Check
// 4) Implement command line interface
// 5) Implement web interface

#include "dht22.h"
#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "locking.h"

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

// main loop
int main()
{
	int lockfd;
	int tries = 100;

	// timer counter for reading
	struct timespec lastReset, currentTime;

	// config data
	int hvacReady = 0;
	int hvacOn = 0;
	int hvacMode = AC;
	int fanMode = ON;
	float heatTemp = 0.0;
	float coolTemp = 0.0;
	float offsetVal = 0.0;

	// data from am2302
	float temperature;
	float humidity;

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
	
	// get current time for lastReset
	clock_gettime(CLOCK_REALTIME, &lastReset);

	// main loop
	while(1)
	{
		// get current time
		clock_gettime(CLOCK_REALTIME, &currentTime);

		// get data from temp sensor
		if((currentTime.tv_sec) - (lastReset.tv_sec) >= 3)
		{
			clock_gettime(CLOCK_REALTIME, &lastReset);
			read_dht22_dat(&temperature, &humidity);
		}

		// check if program has run long enough
		if(clock()/CLOCKS_PER_SEC >= 300)
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
					if(CtoF(temperature)+offsetVal > heatTemp)
					{
						// turn heat on
						hvacOn = 1;
						HeatOn();
					}
					else if(CtoF(temperature)+offsetVal < heatTemp)
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
	}

	delay(1500);
	close_lockfile(lockfd);

	return 0;
}
