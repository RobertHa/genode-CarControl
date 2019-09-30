#include <acc/acc.h>
#include <base/log.h>
#include <util/xml_node.h>
//#include <os/config.h>
#include <base/attached_rom_dataspace.h>
#include <string.h>
#include <errno.h>
#include <acc/types.h>
#include <algorithm>
#include <string.h>
#include <stdio.h>
#include <timer_session/connection.h>

acc::acc(const char* id, Genode::Env &env) : mosquittopp(id)
{
	Timer::Connection timer;
	int starttime = 0;
        int stoptime = 0;
        int duration = 0;
        int totalduration = 0;
        int calculationroundscounter = 0;
        int minval = 0;
        int maxval = 0;
	/* initialization */
	sem_init(&allValSem, 0, 1);
	sem_init(&allData, 0, 0);
	mosqpp::lib_init();

	/* configure mosquitto library */
	Genode::Attached_rom_dataspace config(env, "config");

	Genode::Xml_node mosquitto = config.xml().sub_node("mosquitto");
	try {
		mosquitto.attribute("ip-address").value(this->host, sizeof(host));
	} catch(Genode::Xml_node::Nonexistent_attribute) {
		Genode::error("mosquitto ip-address is missing from config");
	}
	this->port = mosquitto.attribute_value<unsigned int>("port", 1883);
	this->keepalive = mosquitto.attribute_value<unsigned int>("keepalive", 120);

	/* connect to mosquitto server */
	int ret;
	//Genode::log("I am where i connect to mosquitto.....");
	Genode::log("mosquitto host should be at ",(const char *)host);

	do {
		ret = this->connect(host, port, keepalive);
		switch(ret) {
		case MOSQ_ERR_INVAL:
			Genode::error("invalid parameter for mosquitto connect");
			return;
		case MOSQ_ERR_ERRNO:
			Genode::log("mosquitto ", (const char *)strerror(errno));
		default:
			timer.msleep(1000);
			break;
		}
	} while(ret != MOSQ_ERR_SUCCESS);
	
	//Genode::log("We are connected!");

	/* subscribe to topic */
	do {
		ret = this->subscribe(NULL, topic);
		switch(ret) {
		case MOSQ_ERR_INVAL:
			Genode::error("invalid parameter for mosquitto subscribe");
			return;
		case MOSQ_ERR_NOMEM:
			Genode::error("out of memory condition occurred");
			return;
		case MOSQ_ERR_NO_CONN:
			Genode::error("not connected to a broker");
			return;
		}
	} while(ret != MOSQ_ERR_SUCCESS);

	/* start non-blocking loop */
	ret = this->loop_start();
	if (ret != MOSQ_ERR_SUCCESS) {
		switch(ret) {
		case MOSQ_ERR_INVAL:
			Genode::error("invalid parameter for mosquitto loop_start");
			return;
		case MOSQ_ERR_NOT_SUPPORTED:
			Genode::error("mosquitto no thrad support");
			return;
		}
	}

	//Genode::log("I am at main loop!");
	/***************
	 ** main loop **
	 ***************/
	CommandDataOut cdo;  /* command data for the next simulation step */
	char val[512];       /* buffer to convert values to string for mosq */
	snprintf(val, sizeof(val),"%d", 1);
	myPublish("alive", val);
	while(true) {
		/* wait till we get all data */
		
		//Genode::log("I am start waiting for data!");
		sem_wait(&allData);
                starttime = timer.elapsed_ms();
		//Genode::log("I am done waiting, got data!");
		
		/* calculate commanddataout */
		cdo = followDriving(this->sdi);

		/* publish */
		snprintf(val, sizeof(val), "%f", cdo.steer);
		myPublish("steer", val);

		snprintf(val, sizeof(val), "%f", cdo.accel);
		myPublish("accel", val);

		snprintf(val, sizeof(val), "%f", cdo.brakeFL);
		myPublish("brakeFL", val);

		snprintf(val, sizeof(val), "%f", cdo.brakeFR);
		myPublish("brakeFR", val);

		snprintf(val, sizeof(val), "%f", cdo.brakeRL);
		myPublish("brakeRL", val);

		snprintf(val, sizeof(val), "%f", cdo.brakeRR);
		myPublish("brakeRR", val);

		snprintf(val, sizeof(val), "%d", cdo.gear);
		myPublish("gear", val);

                stoptime = timer.elapsed_ms();
                duration = stoptime - starttime;
                totalduration += duration;
                calculationroundscounter++;

                if (calculationroundscounter == 1)
                {
                        minval = duration;
                }
                minval = std::min(minval, duration);
                maxval = std::max(maxval, duration);

                if (calculationroundscounter % 50 == 0)
                {
                        Genode::log("The duration for calculation and sending was ", duration, " milliseconds");
                        Genode::log("The TOTALduration for calculation and sending was ", totalduration, " milliseconds");
                        Genode::log("The MINduration for calculation and sending was ", minval, " milliseconds");
                        Genode::log("The MAXduration for calculation and sending was ", maxval, " milliseconds");
                        Genode::log("The AVERAGEduration for calculation and sending was ", (totalduration / calculationroundscounter), " milliseconds after ", calculationroundscounter, " steps");
                }

	}
}

/* TODO */
acc::~acc() {
}

/**
 * custom publish function
 *
 * \param type  value name
 * \param value value as string
 */
void acc::myPublish(const char *type, const char *value) {
	//Genode::log("I am publishing");
	
	char topic[1024];
	strcpy(topic, "ecu/acc/");
	strncat(topic, type, sizeof(topic));
	publish(NULL, topic, strlen(value), value);
}

void acc::on_message(const struct mosquitto_message *message)
{
	//Genode::log("I got a message!");
	/* split type from topic */
	char *type = strrchr(message->topic, '/') + 1;
	/* get pointer to payload for convenience */
	char *value = (char *)message->payload;

	/* split x,y into two separate values */
	float x = 0.0, y = 0.0;
	if (strstr(value, ",")) {
		x = atof(strtok(value, ","));
		y = atof(strtok(NULL, ","));
	}


	/* fill sensorDataIn struct */
	if (!strcmp(type, "isPositionTracked")) {
		sdi.isPositionTracked = atoi(value);
	} else if (!strcmp(type, "isSpeedTracked")) {
		sdi.isSpeedTracked = atoi(value);
	} else if (!strcmp(type, "leadPos")) {
		sdi.leadPos = vec2(x, y);
	} else if (!strcmp(type, "ownPos")) {
		sdi.ownPos = vec2(x, y);
	} else if (!strcmp(type, "cornerFrontRight")) {
		sdi.cornerFrontRight = vec2(x, y);
	} else if (!strcmp(type, "cornerFrontLeft")) {
		sdi.cornerFrontLeft = vec2(x, y);
	} else if (!strcmp(type, "cornerRearLeft")) {
		sdi.cornerRearLeft = vec2(x, y);
	} else if (!strcmp(type, "cornerRearRight")) {
		sdi.cornerRearRight = vec2(x, y);
	} else if (!strcmp(type, "leadSpeed")) {
		sdi.leadSpeed = atof(value);
	} else if (!strcmp(type, "ownSpeed")) {
		sdi.ownSpeed = atof(value);
	} else if (!strcmp(type, "curGear")) {
		sdi.curGear = atoi(value);
	} else if (!strcmp(type, "steerLock")) {
		sdi.steerLock = atof(value);
	} else {
		//Genode::log("unknown topic: ", (const char *)message->topic);
		return;
	}

	/* check if we got all values */
	sem_wait(&allValSem);
	allValues = (allValues + 1) % 12;
	if (!allValues) {
		sem_post(&allData);
	}
	sem_post(&allValSem);
}

void acc::on_connect(int rc)
{
	Genode::log("connected to mosquitto server");
}

void acc::on_disconnect(int rc)
{
	Genode::log("disconnect from mosquitto server");
}

/**
 * calculates the gear
 * based on empiric values? of the speed
 *
 * took it 1:1 from https://github.com/argos-research/genode-Simcom
 */
int acc::getSpeedDepGear(float speed, int currentGear)
{
	// 0	 60  100 150 200 250 km/h
	float gearUP[6] = {-1, 17, 27, 41, 55, 70}; //Game uses values in m/s: xyz m/s = (3.6 * xyz) km/h
	float gearDN[6] = {0,	 0,  15, 23, 35, 48};

	int gear = currentGear;

	if (speed > gearUP[gear])
	{
		gear = std::min(5, currentGear + 1);
	}
	if (speed < gearDN[gear])
	{
		gear = std::max(1, currentGear - 1);
	}
	return gear;
}

/**
 * calculates command data for the next simulation step
 * based on the sensor data from the previous step
 *
 * took it 1:1 from https://github.com/argos-research/genode-Simcom
 */
CommandDataOut acc::followDriving(SensorDataIn sd)
{
	// Get position of nearest opponent in front
	// via: - Sensor data
	// If distance below certain threshold
	// Drive in that direction (set angle)
	// If position in previous frame is known:
	//	 Calculate speed from old and new world positon
	//	 Try to adjust accel and brake to match speed of opponent
	//	 (Try to shift gear accordingly)
	// Save new world position in old position
	CommandDataOut cd = {0};

	if(!sd.isPositionTracked)
	{
		return cd;
	}
	vec2 curLeadPos = sd.leadPos;
	vec2 ownPos = sd.ownPos;

	// Get point of view axis of car in world coordinates
	// by substracting the positon of front corners and position of rear corners
	vec2 axis = (sd.cornerFrontRight - sd.cornerRearRight) + (sd.cornerFrontLeft - sd.cornerRearLeft);
	axis.normalize();

	//Get angle beween view axis and curleadPos to adjust steer
	vec2 leadVec = curLeadPos - ownPos;
	float dist = leadVec.len(); // absolute distance between cars

	// printf("DISTANCE: %f\n", leadVec.len());
	leadVec.normalize();


	// printf("CROSS: %f\n", axis.fakeCrossProduct(&leadVec));
	// printf("ANGLE: %f\n", RAD2DEG(asin(axis.fakeCrossProduct(&leadVec))));

	const float cross = axis.fakeCrossProduct(&leadVec);
	const float dot = axis * leadVec;
	const float angle = std::atan2(cross, dot) / sd.steerLock / 2.0;

	cd.steer = angle; // Set steering angle

	// Only possible to calculate accel and brake if speed of leading car known
	if(!sd.isSpeedTracked) // If position of leading car known in last frame
	{
		return cd;
	}

	float fspeed = sd.ownSpeed; // speed of following car

	float lspeed = sd.leadSpeed; // speed of leading car

	// adjusted distance to account for different speed, but keep it positive so brake command will not be issued if leading speed is too high
	//float adist = std::max<float>(0.1, g_followDist + (fspeed - lspeed));

	// Accel gets bigger if we are further away from the leading car
	// Accel goes to zero if we are at the target distance from the leading car
	// Target distance is adjusted, dependent on the the speed difference of both cars
	// Accel = maxAccel if dist = threshold
	// Accel = 0 if dist = adist (adjusted target dist)
	//cd.accel = std::max<float>(0, std::min<float>(g_maxAccel, std::sqrt(g_maxAccel * (dist - adist) / (g_distThreshold - adist))));


	// Äquivalent to accel but the other way round
	//float b = std::max<float>(0, std::min<float>(g_maxBrake, std::sqrt(g_maxBrake * (adist - dist) / adist)));
	float dv = (lspeed - fspeed);
	if (dv > 0.0 && dist > 5.0)
	{
		cd.accel = 0.5 * dv;
	}
	else if (dist < 30.0)
	{
		cd.brakeFL = cd.brakeFR = cd.brakeRL = cd.brakeRR = -0.5 * dv;
	}
	//char format[256];
	//sprintf(format, "Speeds: %4.4f %4.4f %4.4f\n", lspeed, fspeed, dist);
	//PDBG("%s", format);

	// Individual brake commands for each wheel

	cd.gear = getSpeedDepGear(sd.ownSpeed, sd.curGear);

	return cd;
}
