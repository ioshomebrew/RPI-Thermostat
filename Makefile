all:
	gcc main.c dht22.c locking.c -l wiringPi -o thermostat
