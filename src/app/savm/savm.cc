#include <savm/savm.h>
#include <base/log.h>
#include <util/xml_node.h>
#include <os/config.h>
#include <string.h>
#include <errno.h>

extern "C" {
#include <lwip/sockets.h>
}
#include <lwip/genode.h>

void savm::readAllBytes(void *buf, int socket, unsigned int size) {
	unsigned int offset = 0;
	int ret = 0;

	do {
		ret = lwip_read(socket, (char *)buf + offset, size - offset);
		if (ret == -1) {
			Genode::error("lwip_read failed: ", (const char *)strerror(errno));
		} else {
			offset += ret;
		}
	} while(offset != size);
}

savm::savm(const char *id) : mosquittopp(id)
{
	sem_init(&allValSem, 0, 1);
	sem_init(&allData, 0, 0);
	mosqpp::lib_init();  /* initialize mosquitto library */

	/* configure mosquitto library */
	Genode::Xml_node mosquitto = Genode::config()->xml_node().sub_node("mosquitto");
	try {
		mosquitto.attribute("ip-address").value(this->host, sizeof(host));
	} catch(Genode::Xml_node::Nonexistent_attribute) {
		Genode::error("mosquitto ip-address is missing from config");
	}
	this->port = mosquitto.attribute_value<unsigned int>("port", 1883);
	this->keepalive = mosquitto.attribute_value<unsigned int>("keepalive", 60);

	/* connect to mosquitto server */
	int ret;
	do {
		ret = this->connect(host, port, keepalive);
		switch(ret) {
		case MOSQ_ERR_INVAL:
			Genode::error("invalid parameter for mosquitto connect");
			return;
		case MOSQ_ERR_ERRNO:
			break;
			Genode::log("mosquitto ", (const char *)strerror(errno));
		}
	} while(ret != MOSQ_ERR_SUCCESS);

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
	
	/* subscribe to topic */
	do {
		ret = this->subscribe(NULL, notify_topic);
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

	/* start non-blocking mosquitto loop */
	loop_start();

	/***********************
	 ** Connection to SD2 **
	 ***********************/
	int sock;

	char ip_addr[16] = { 0 };
	unsigned int port = 0;

	Genode::Xml_node speeddreams = Genode::config()->xml_node().sub_node("speed-dreams");
	try {
		speeddreams.attribute("ip-address").value(ip_addr, sizeof(ip_addr));
		speeddreams.attribute("port").value<unsigned int>(&port);
	} catch(Genode::Xml_node::Nonexistent_attribute) {
		Genode::error("please check speed-dreams configuration");
	}
	
	struct sockaddr_in srv_addr;
	bzero(&srv_addr, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = inet_addr(ip_addr);
	srv_addr.sin_port = htons(port);

	if ((sock = lwip_socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		Genode::error("socket failed: ", (const char *)strerror(errno));
	}

	while((ret = lwip_connect(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) == -1)) {
		Genode::error("connect failed: ", (const char *)strerror(errno));
		lwip_close(sock);
		if ((sock = lwip_socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			Genode::error("socket failed: ", (const char *)strerror(errno));
		}
	}

	/* disable Nagle's algorithm */
	int flag = 1;
   	int result = lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
	if (result == -1) {
		Genode::error("setsockopt failed: ", (const char *)strerror(errno));
	}

	/******************
	 ** Message flow **
	 ******************/
	uint32_t msg_len = 0;
	std::string cdi_str;
	char buffer[1024] = { 0 };
	while(true) {
		readAllBytes(&msg_len, sock, sizeof(msg_len));
		msg_len = ntohl(msg_len);

		buffer[msg_len] = { '\0' };
		readAllBytes(buffer, sock, msg_len);

		starttime = timer.elapsed_ms();
		/*******************
		 ** SensorDataOut **
		 *******************/
		sdo.ParseFromArray(buffer, msg_len);

		publishAllData();

		/*******************
		 ** CommandDataIn **
		 *******************/
		/* wait for data */
		initialized = true;
		sem_wait(&allData);

		/* prepare message */
		cdi_str.clear();                       // clear cdi_str
		msg_len = htonl(cdi.ByteSizeLong());   // get cdi length in bytes (network byte order)
		cdi_str.append((const char*)&msg_len,
					   sizeof(msg_len));       // append message length
		cdi.AppendToString(&cdi_str);          // append cdi
		msg_len = cdi_str.size();              // get #bytes to transmit
		
		Genode::log("To SD2 message: ", cdi.accel()); // provides feedback that steps are performmed
		/* send message */
		int ret = lwip_write(sock, cdi_str.c_str(), msg_len);
		if (ret == -1) {
			Genode::error("write cdi failed! ", (const char *)strerror(errno));
		} else if ((unsigned int)ret < msg_len) {
			Genode::error("write cdi failed to send complete message! ",
						  ret,
						  " vs. ",
						  msg_len);
		}
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


void savm::publishAllData(){
	char val[512] = { 0 };
	snprintf(val, sizeof(val), "%d", sdo.ispositiontracked());
	myPublish("isPositionTracked", val);

	snprintf(val, sizeof(val), "%d", sdo.isspeedtracked());
	myPublish("isSpeedTracked", val);

	vec2 = sdo.leadpos();
	snprintf(val, sizeof(val), "%f,%f", vec2.x(), vec2.y());
	myPublish("leadPos", val);

	vec2 = sdo.ownpos();
	snprintf(val, sizeof(val), "%f,%f", vec2.x(), vec2.y());
	myPublish("ownPos", val);

	vec2 = sdo.cornerfrontright();
	snprintf(val, sizeof(val), "%f,%f", vec2.x(), vec2.y());
	myPublish("cornerFrontRight", val);

	vec2 = sdo.cornerfrontleft();
	snprintf(val, sizeof(val), "%f,%f", vec2.x(), vec2.y());
	myPublish("cornerFrontLeft", val);

	vec2 = sdo.cornerrearright();
	snprintf(val, sizeof(val), "%f,%f", vec2.x(), vec2.y());
	myPublish("cornerRearRight", val);

	vec2 = sdo.cornerrearleft();
	snprintf(val, sizeof(val), "%f,%f", vec2.x(), vec2.y());
	myPublish("cornerRearLeft", val);

	snprintf(val, sizeof(val), "%f", sdo.leadspeed());
	myPublish("leadSpeed", val);

	snprintf(val, sizeof(val), "%f", sdo.ownspeed());
	myPublish("ownSpeed", val);

	snprintf(val, sizeof(val), "%d", sdo.curgear());
	myPublish("curGear", val);

	snprintf(val, sizeof(val), "%f", sdo.steerlock());
	myPublish("steerLock", val);

	snprintf(val, sizeof(val), "%f", sdo.enginerpm());
	myPublish("enginerpm", val);

	snprintf(val, sizeof(val), "%f", sdo.enginerpmmax());
	myPublish("enginerpmMax", val);

	snprintf(val, sizeof(val), "%f", sdo.steer());
	myPublish("steer", val);

	snprintf(val, sizeof(val), "%f", sdo.brakefl());
	myPublish("brakeFL", val);

	snprintf(val, sizeof(val), "%f", sdo.brakefr());
	myPublish("brakeFR", val);

	snprintf(val, sizeof(val), "%f", sdo.brakerl());
	myPublish("brakeRL", val);

	snprintf(val, sizeof(val), "%f", sdo.brakerr());
	myPublish("brakeRR", val);

	allValues = 0;
}

void savm::myPublish(const char *type, const char *value) {
	char topic[1024];
	strcpy(topic, "savm/car/0/");
	strncat(topic, type, sizeof(topic));
	publish(NULL, topic, strlen(value), value);
}

savm::~savm() {
}

void savm::on_message(const struct mosquitto_message *message)
{
	char *type = strrchr(message->topic, '/') + 1;
	char *value = (char *)message->payload;

	if (!strcmp(type, "steer")) {
		cdi.set_steer(atof(value));
	} else if (!strcmp(type, "accel")) {
		cdi.set_accel(atof(value));
	} else if (!strcmp(type, "brakeFL")) {
		cdi.set_brakefl(atof(value));
	} else if (!strcmp(type, "brakeFR")) {
		cdi.set_brakefr(atof(value));
	} else if (!strcmp(type, "brakeRL")) {
		cdi.set_brakerl(atof(value));
	} else if (!strcmp(type, "brakeRR")) {
		cdi.set_brakerr(atof(value));
	} else if (!strcmp(type, "gear")) {
		cdi.set_gear(atoi(value));
	} else if (!strcmp(type, "alive")) {
		if (initialized) {
			publishAllData();
			Genode::log("initialized is true");
		}
		Genode::log("received topic: ", (const char *)message->topic);
		return;
	} else {
		Genode::log("unknown topic: ", (const char *)message->topic);
		return;
	}

	sem_wait(&allValSem);
	allValues = (allValues + 1) % 7;
	if (!allValues) {
		sem_post(&allData);
	}
	sem_post(&allValSem);
}

void savm::on_connect(int rc)
{
	Genode::log("connected to mosquitto server");
}

void savm::on_disconnect(int rc)
{
	Genode::log("disconnect from mosquitto server");
}
