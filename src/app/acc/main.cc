#include <base/component.h>
#include <util/xml_node.h>
//#include <os/config.h>
#include <base/attached_rom_dataspace.h>
#include <acc/acc.h>
#include <nic/packet_allocator.h>
#include <lwip/genode.h>
#include <libc/component.h>
extern "C" {
#include <lwip/sockets.h>
#include <lwip/api.h>
}

Genode::size_t Component::stack_size() { return 64*1024; }

void Libc::Component::construct(Libc::Env &env)
{
	Genode::log("I am alive!");
	enum { BUF_SIZE = Nic::Packet_allocator::DEFAULT_PACKET_SIZE * 128 };

	Genode::Attached_rom_dataspace config(env, "config");
	Genode::Xml_node network = config.xml().sub_node("network");

	if (network.attribute_value<bool>("dhcp", true)) {
		if (lwip_nic_init(0,
						  0,
						  0,
						  BUF_SIZE,
						  BUF_SIZE)) {
			return;
		}
	} else {
		char ip_addr[16] = { 0 };
		char subnet[16] = { 0 };
		char gateway[16] = { 0 };

		try {
			network.attribute("ip-address").value(ip_addr, sizeof(ip_addr));
			network.attribute("subnet").value(subnet, sizeof(subnet));
			network.attribute("gateway").value(gateway, sizeof(gateway));
		} catch(Genode::Xml_node::Nonexistent_attribute) {
			Genode::error("please check the network configuration");
			return;
		}
	}

	acc acc("acc", env);
}
