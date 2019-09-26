#include <mosquittopp.h>
#include <acc/types.h>
#include <semaphore.h>
#include <base/env.h>

class acc : public mosqpp::mosquittopp
{
	private:
	/* mosquitto */
	char host[16];
	const char* id = "acc";
	const char* topic = "savm/car/0/+";
	int port;
	int keepalive;
	enum {
		VEL_OFF  = 0,
		VEL_STEP = 1,
		VEL_MIN  = 10,
		VEL_MAX  = 30,
	};
	float g_distThreshold = 50.0; //50m
	float g_followDist = 10.0; // 10m
	float g_maxAccel = 1.5;
	float g_maxBrake = 1.0;
	int allValues;
	sem_t allValSem;
	sem_t allData;
	struct SensorDataIn sdi = {0};

public:
	acc(const char *id, Genode::Env &env);
	~acc();
private:
	void on_connect(int rc);
	void on_disconnect(int rc);
	void on_message(const struct mosquitto_message *message);
	int getSpeedDepGear(float speed, int currentGear);
	CommandDataOut followDriving(SensorDataIn sd);
	void myPublish(const char *type, const char *value);
};
