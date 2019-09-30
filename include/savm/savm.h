#include <mosquittopp.h>
#include <savm/SensorDataOut.pb.h>
#include <savm/CommandDataIn.pb.h>
#include <semaphore.h>
#include <timer_session/connection.h>

/* fix redefinition of struct timeval */
#define timeval _timeval

class savm : public mosqpp::mosquittopp
{
	private:
	/* mosquitto */
	char host[16];
	const char* id = "savm";
	const char* topic = "ecu/acc/#";
	const char* notify_topic = "test";
	int port;
	int keepalive;

	protobuf::CommandDataIn cdi;
	protobuf::SensorDataOut sdo;
	protobuf::SensorDataOut_vec2 vec2;
	
	int allValues;
	sem_t allValSem;
	sem_t allData;
	bool initialized = false;

	Timer::Connection timer;
	int starttime = 0;
	int stoptime = 0;
	int duration = 0;
	int totalduration = 0;
	int calculationroundscounter = 0;
	int minval = 0;
	int maxval = 0;

	void on_connect(int rc);
	void on_disconnect(int rc);
	void on_message(const struct mosquitto_message *message);

	void readAllBytes(void *buf, int socket, unsigned int size);
	void myPublish(const char *type, const char *value);
	void publishAllData();

	public:
	savm(const char* id);
	~savm();
};
