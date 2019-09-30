#include <base/component.h>
#include <util/xml_node.h>
#include <base/attached_rom_dataspace.h>
#include <acc/acc.h>
#include <libc/component.h>


void Libc::Component::construct(Libc::Env &env)
{
	Genode::log("I am alive!");

	acc acc("acc", env);

	Genode::log("i am done here!");
}
