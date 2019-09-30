#include <base/component.h>
#include <util/xml_node.h>
//#include <os/config.h>
#include <base/attached_rom_dataspace.h>
#include <acc/acc.h>
#include <libc/component.h>

//struct test_thread : public Genode::Thread
//{
//	private:
//		Genode::Env &_env;
//
//	public:
//		test_thread(Genode::Env &env) : Genode::Thread(env, "test", 16 * 1024), _env(env) { }
//		void entry()
//		{
//			acc acc("acc", _env);
//		}
//};

void Libc::Component::construct(Libc::Env &env)
{
	Genode::log("I am alive!");

	acc acc("acc", env);
	//static auto thread = new test_thread(env);
	//thread->start();

	Genode::log("i am done here!");
}
