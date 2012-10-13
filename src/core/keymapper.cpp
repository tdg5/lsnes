#include "core/command.hpp"
#include "core/dispatch.hpp"
#include "library/globalwrap.hpp"
#include "core/keymapper.hpp"
#include "core/memorymanip.hpp"
#include "core/misc.hpp"
#include "core/window.hpp"
#include "lua/lua.hpp"
#include "library/string.hpp"

#include <stdexcept>
#include <stdexcept>
#include <iostream>
#include <list>
#include <map>
#include <sstream>
#include <set>

namespace
{
	function_ptr_command<const std::string&> bind_key("bind-key", "Bind a (pseudo-)key",
		"Syntax: bind-key [<mod>/<modmask>] <key> <command>\nBind command to specified key (with specified "
		" modifiers)\n",
		[](const std::string& t) throw(std::bad_alloc, std::runtime_error) {
			auto r = regex("(([^ /\t]*)/([^ /\t]*)[ \t]+)?([^ \t]+)[ \t]+([^ \t].*)", t,
				"Key and command required");
			keymapper::bind(r[2], r[3], r[4], r[5]);
			if(r[2] != "" || r[3] != "")
				messages << r[2] << "/" << r[3] << " ";
			messages << r[4] << " bound to '" << r[5] << "'" << std::endl;
		});

	function_ptr_command<const std::string&> unbind_key("unbind-key", "Unbind a (pseudo-)key",
		"Syntax: unbind-key [<mod>/<modmask>] <key>\nUnbind specified key (with specified modifiers)\n",
		[](const std::string& t) throw(std::bad_alloc, std::runtime_error) {
			auto r = regex("(([^ /\t]*)/([^ /\t]*)[ \t]+)?([^ \t]+)[ \t]*", t, "Key required");
			keymapper::unbind(r[2], r[3], r[4]);
			if(r[2] != "" || r[3] != "")
				messages << r[2] << "/" << r[3] << " ";
			messages << r[4] << " unbound" << std::endl;
		});

	function_ptr_command<> show_bindings("show-bindings", "Show active bindings",
		"Syntax: show-bindings\nShow bindings that are currently active.\n",
		[]() throw(std::bad_alloc, std::runtime_error) {
			keymapper::dumpbindings();
		});

	function_ptr_command<> show_inverse("show-inverse", "Show inverse bindings",
		"Syntax: show-inverse\nShow inversebindings that are currently active.\n",
		[]() throw(std::bad_alloc, std::runtime_error) {
			for(auto i : inverse_key::get_ikeys())
				messages << i->getname() << ":" << i->get(true) << ":" << i->get(false) << std::endl;
		});
}

std::string fixup_command_polarity(std::string cmd, bool polarity) throw(std::bad_alloc)
{
	if(cmd == "" || cmd == "*")
		return "";
	if(cmd[0] != '*') {
		if(cmd[0] != '+' && polarity)
			return "";
		if(cmd[0] == '+' && !polarity)
			cmd[0] = '-';
	} else {
		if(cmd[1] != '+' && polarity)
			return "";
		if(cmd[1] == '+' && !polarity)
			cmd[1] = '-';
	}
	return cmd;
}

namespace
{
	globalwrap<std::map<std::string, modifier*>> known_modifiers;
	globalwrap<std::map<std::string, std::string>> modifier_linkages;
	globalwrap<std::map<std::string, keygroup*>> keygroups;

	//Return the recursive mutex.
	mutex& kmlock()
	{
		static mutex& m = mutex::aquire_rec();
		return m;
	}

	class kmlock_hold
	{
	public:
		kmlock_hold() { kmlock().lock(); }
		~kmlock_hold() { kmlock().unlock(); }
	private:
		kmlock_hold(const kmlock_hold& k);
		kmlock_hold& operator=(const kmlock_hold& k);
	};

	//Returns orig if not linked.
	const modifier* get_linked_modifier(const modifier* orig)
	{
		if(!modifier_linkages().count(orig->name()))
			return orig;
		std::string l = modifier_linkages()[orig->name()];
		return known_modifiers()[l];
	}
}

modifier::modifier(const std::string& name) throw(std::bad_alloc)
{
	kmlock_hold lck;
	known_modifiers()[modname = name] = this;
}

modifier::modifier(const std::string& name, const std::string& linkgroup) throw(std::bad_alloc)
{
	kmlock_hold lck;
	known_modifiers()[modname = name] = this;
	modifier_linkages()[name] = linkgroup;
}

modifier::~modifier() throw()
{
	kmlock_hold lck;
	known_modifiers().erase(modname);
	modifier_linkages().erase(modname);
}

std::set<std::string> modifier::get_set() throw(std::bad_alloc)
{
	kmlock_hold lck;
	std::set<std::string> r;
	for(auto i : known_modifiers())
		r.insert(i.first);
	return r;
}

modifier& modifier::lookup(const std::string& name) throw(std::bad_alloc, std::runtime_error)
{
	kmlock_hold lck;
	if(!known_modifiers().count(name)) {
		std::ostringstream x;
		x << "Invalid modifier '" << name << "'";
		throw std::runtime_error(x.str());
	}
	return *known_modifiers()[name];
}

std::string modifier::name() const throw(std::bad_alloc)
{
	kmlock_hold lck;
	return modname;
}

std::string modifier::linked_name() const throw(std::bad_alloc)
{
	kmlock_hold lck;
	const modifier* p = get_linked_modifier(this);
	if(p == this)
		return "";
	return p->modname;
}


void modifier_set::add(const modifier& mod, bool really) throw(std::bad_alloc)
{
	kmlock_hold lck;
	if(really)
		set.insert(&mod);
}

void modifier_set::remove(const modifier& mod, bool really) throw(std::bad_alloc)
{
	kmlock_hold lck;
	if(really)
		set.erase(&mod);
}

modifier_set modifier_set::construct(const std::string& _modifiers) throw(std::bad_alloc, std::runtime_error)
{
	kmlock_hold lck;
	modifier_set set;
	std::string modifiers = _modifiers;
	while(modifiers != "") {
		std::string mod = modifiers;
		std::string rest;
		size_t split = modifiers.find_first_of(",");
		if(split < modifiers.length()) {
			mod = modifiers.substr(0, split);
			rest = modifiers.substr(split + 1);
		}
		set.add(modifier::lookup(mod));
		modifiers = rest;
	}
	return set;
}

bool modifier_set::valid(const modifier_set& set, const modifier_set& mask) throw(std::bad_alloc)
{
	kmlock_hold lck;
	//No element can be together with its linkage group.
	for(auto i : set.set) {
		const modifier* j = get_linked_modifier(i);
		if(i != j && set.set.count(j))
			return false;
	}
	for(auto i : mask.set) {
		const modifier* j = get_linked_modifier(i);
		if(i != j && mask.set.count(j))
			return false;
	}
	//For every element of set, it or its linkage group must be in mask.
	for(auto i : set.set) {
		const modifier* j = get_linked_modifier(i);
		if(!mask.set.count(i) && !mask.set.count(j))
			return false;
	}
	return true;
}

bool modifier_set::operator==(const modifier_set& m) const throw()
{
	kmlock_hold lck;
	for(auto i : set)
		if(!m.set.count(i))
			return false;
	for(auto i : m.set)
		if(!set.count(i))
			return false;
	return true;
}

std::ostream&  operator<<(std::ostream& os, const modifier_set& m)
{
	kmlock_hold lck;
	os << "<modset:";
	for(auto i : m.set)
		os << i->name() << " ";
	os << ">";
	return os;
}

bool modifier_set::triggers(const modifier_set& set, const modifier_set& trigger, const modifier_set& mask)
	throw(std::bad_alloc)
{
	kmlock_hold lck;
	for(auto i : mask.set) {
		bool ok = false;
		//OK iff at least one of:
		//Key itself appears in both set and trigger.
		for(auto j : set.set) {
			if(!trigger.set.count(j))
				continue;
			ok = true;
		}
		//Key with this linkage group appears in both set and trigger.
		for(auto j : set.set) {
			auto linked = get_linked_modifier(j);
			if(linked != i)
				continue;
			if(!trigger.set.count(j))
				continue;
			ok = true;
		}
		//Key with this linkage appears in set and the key itself appears in trigger.
		for(auto j : set.set) {
			auto linked = get_linked_modifier(j);
			if(linked != i)
				continue;
			if(!trigger.set.count(i))
				continue;
			ok = true;
		}
		//Nothing linked is found from neither set nor trigger.
		bool found = false;
		for(auto j : set.set)
			found = found || (j == i || get_linked_modifier(j) == i);
		for(auto j : trigger.set)
			found = found || (j == i || get_linked_modifier(j) == i);
		ok = ok || !found;
		if(!ok)
			return false;
	}
	return true;
}

std::string keygroup::name() throw(std::bad_alloc)
{
	kmlock_hold lck;
	return keyname;
}

const std::string& keygroup::get_class()
{
	kmlock_hold lck;
	return clazz;
}

struct keygroup::parameters keygroup::get_parameters()
{
	kmlock_hold lck;
	parameters p;
	p.ktype = ktype;
	p.cal_left = cal_left;
	p.cal_center = cal_center;
	p.cal_right = cal_right;
	p.cal_tolerance = cal_tolerance;
	p.last_rawval = last_rawval;
	return p;
}

std::map<std::string, struct keygroup::parameters> keygroup::get_all_parameters()
{
	kmlock_hold lck;
	std::map<std::string, struct parameters> ret;
	for(auto i : keygroups())
		ret[i.first] = i.second->get_parameters();
	return ret;
}

keygroup::keygroup(const std::string& name, const std::string& _clazz, enum type t) throw(std::bad_alloc)
{
	kmlock_hold lck;
	keygroups()[keyname = name] = this;
	clazz = _clazz;
	ktype = t;
	state = 0;
	last_rawval = 0;
	cal_left = -32768;
	cal_center = 0;
	cal_right = 32767;
	cal_tolerance = 0.5;
	requests_hook = false;
}

keygroup::~keygroup() throw()
{
	kmlock_hold lck;
	keygroups().erase(keyname);
}

void keygroup::change_type(enum type t) throw()
{
	kmlock_hold lck;
	ktype = t;
	state = 0;
	if(requests_hook)
		lua_callback_keyhook(keyname, get_parameters());
}

void keygroup::request_hook_callback(bool state)
{
	kmlock_hold lck;
	requests_hook = state;
}


std::pair<keygroup*, unsigned> keygroup::lookup(const std::string& name) throw(std::bad_alloc,
	std::runtime_error)
{
	kmlock_hold lck;
	if(keygroups().count(name))
		return std::make_pair(keygroups()[name], 0);
	std::string prefix = name;
	char letter = prefix[prefix.length() - 1];
	prefix = prefix.substr(0, prefix.length() - 1);
	if(!keygroups().count(prefix))
		throw std::runtime_error("Invalid key");
	keygroup* g = keygroups()[prefix];
	switch(letter) {
	case '+':
	case 'n':
		return std::make_pair(g, 0);
	case '-':
	case 'e':
		return std::make_pair(g, 1);
	case 's':
		return std::make_pair(g, 2);
	case 'w':
		return std::make_pair(g, 3);
	default:
		throw std::runtime_error("Invalid key");
	}
}

void keygroup::change_calibration(short left, short center, short right, double tolerance)
{
	kmlock_hold lck;
	cal_left = left;
	cal_center = center;
	cal_right = right;
	cal_tolerance = tolerance;
	if(requests_hook)
		lua_callback_keyhook(keyname, get_parameters());
}

double keygroup::compensate(short value)
{
	kmlock_hold lck;
	if(ktype == KT_HAT || ktype == KT_KEY || ktype == KT_DISABLED || ktype == KT_MOUSE)
		return value;	//These can't be calibrated.
	if(value <= cal_left)
		return -1.0;
	else if(value >= cal_right)
		return 1.0;
	else if(value == cal_center)
		return 0.0;
	else if(value < cal_center)
		return (static_cast<double>(value) - cal_center) / (static_cast<double>(cal_center) - cal_left);
	else
		return (static_cast<double>(value) - cal_center) / (static_cast<double>(cal_right) - cal_center);
}

double keygroup::compensate2(double value)
{
	kmlock_hold lck;
	switch(ktype) {
	case KT_DISABLED:
	case KT_MOUSE:
		return 0;	//Always neutral.
	case KT_KEY:
	case KT_HAT:
		return value;	//No mapping.
	case KT_PRESSURE_0M:
		return -value;
	case KT_PRESSURE_0P:
		return value;
	case KT_PRESSURE_M0:
		return 1 + value;
	case KT_PRESSURE_MP:
		return (1 + value) / 2;
	case KT_PRESSURE_P0:
		return 1 - value;
	case KT_PRESSURE_PM:
		return (1 - value) / 2;
	case KT_AXIS_PAIR:
		return value;
	case KT_AXIS_PAIR_INVERSE:
		return -value;
	};
}

void keygroup::set_position(short pos, const modifier_set& modifiers) throw()
{
	kmlock_hold lck;
	last_rawval = pos;
	if(requests_hook)
		lua_callback_keyhook(keyname, get_parameters());
	double x = compensate2(compensate(pos));
	signed tmp;
	bool left, right, up, down;
	bool oleft, oright, oup, odown;
	switch(ktype) {
	case KT_DISABLED:
		return;
	case KT_MOUSE:
		state = pos - cal_center;
		break;
	case KT_KEY:
	case KT_PRESSURE_0M:
	case KT_PRESSURE_0P:
	case KT_PRESSURE_M0:
	case KT_PRESSURE_MP:
	case KT_PRESSURE_P0:
	case KT_PRESSURE_PM:
		tmp = (x >= cal_tolerance);
		run_listeners(modifiers, 0, true, (!state && tmp), x);
		run_listeners(modifiers, 0, false, (state && !tmp), x);
		state = tmp;
		break;
	case KT_AXIS_PAIR:
	case KT_AXIS_PAIR_INVERSE:
		if(x <= -cal_tolerance)
			tmp = 2;
		else if(x >= cal_tolerance)
			tmp = 1;
		else
			tmp = 0;
		run_listeners(modifiers, 0, false, state == 1 && tmp != 1, x);
		run_listeners(modifiers, 1, false, state == 2 && tmp != 2, x);
		run_listeners(modifiers, 0, true, tmp == 1 && state != 1, x);
		run_listeners(modifiers, 1, true, tmp == 2 && state != 2, x);
		state = tmp;
		break;
	case KT_HAT:
		left = ((pos & 8) != 0);
		right = ((pos & 2) != 0);
		up = ((pos & 1) != 0);
		down = ((pos & 4) != 0);
		oleft = ((state & 8) != 0);
		oright = ((state & 2) != 0);
		oup = ((state & 1) != 0);
		odown = ((state & 4) != 0);
		run_listeners(modifiers, 3, false, oleft && !left, x);
		run_listeners(modifiers, 1, false, oright && !right, x);
		run_listeners(modifiers, 0, false, oup && !up, x);
		run_listeners(modifiers, 2, false, odown && !down, x);
		run_listeners(modifiers, 2, true, !odown && down, x);
		run_listeners(modifiers, 0, true, !oup && up, x);
		run_listeners(modifiers, 1, true, !oright && right, x);
		run_listeners(modifiers, 3, true, !oleft && left, x);
		state = pos;
		break;
	}
}

void keygroup::run_listeners(const modifier_set& modifiers, unsigned subkey, bool polarity, bool really, double x)
{
	std::string name;
	modifier_set _modifiers = modifiers;
	{
		if(!really)
			return;
		kmlock_hold lck;
		name = keyname;
		if(ktype == KT_AXIS_PAIR && subkey == 0)
			name = name + "+";
		if(ktype == KT_AXIS_PAIR && subkey == 1)
			name = name + "-";
		if(ktype == KT_HAT && subkey == 0)
			name = name + "n";
		if(ktype == KT_HAT && subkey == 1)
			name = name + "e";
		if(ktype == KT_HAT && subkey == 2)
			name = name + "s";
		if(ktype == KT_HAT && subkey == 3)
			name = name + "w";
	}
	information_dispatch::do_key_event(_modifiers, *this, subkey, polarity, name);
}

keygroup* keygroup::lookup_by_name(const std::string& name) throw()
{
	kmlock_hold lck;
	if(keygroups().count(name))
		return keygroups()[name];
	else
		return NULL;
}

std::set<std::string> keygroup::get_axis_set() throw(std::bad_alloc)
{
	kmlock_hold lck;
	std::set<std::string> r;
	for(auto i : keygroups()) {
		keygroup::parameters p = i.second->get_parameters();
		std::string type = "";
		switch(p.ktype) {
		case keygroup::KT_DISABLED:
		case keygroup::KT_AXIS_PAIR:
		case keygroup::KT_AXIS_PAIR_INVERSE:
		case keygroup::KT_PRESSURE_0M:
		case keygroup::KT_PRESSURE_0P:
		case keygroup::KT_PRESSURE_M0:
		case keygroup::KT_PRESSURE_MP:
		case keygroup::KT_PRESSURE_P0:
		case keygroup::KT_PRESSURE_PM:
			r.insert(i.first);
			break;
		default:
			break;
		};
	}
	return r;
}

std::set<std::string> keygroup::get_keys() throw(std::bad_alloc)
{
	kmlock_hold lck;
	std::set<std::string> r;
	for(auto i : keygroups()) {
		switch(i.second->ktype) {
		case KT_KEY:
		case KT_PRESSURE_M0:
		case KT_PRESSURE_MP:
		case KT_PRESSURE_0M:
		case KT_PRESSURE_0P:
		case KT_PRESSURE_PM:
		case KT_PRESSURE_P0:
			r.insert(i.first);
			break;
		case KT_AXIS_PAIR:
		case KT_AXIS_PAIR_INVERSE:
			r.insert(i.first + "+");
			r.insert(i.first + "-");
			break;
		case KT_HAT:
			r.insert(i.first + "n");
			r.insert(i.first + "e");
			r.insert(i.first + "s");
			r.insert(i.first + "w");
			break;
		default:
			break;
		};
	}
	return r;
}

signed keygroup::get_value()
{
	kmlock_hold lck;
	return state;
}

namespace
{
	function_ptr_command<const std::string&> set_axis("set-axis", "Set mode of Joystick axis",
		"Syntax: set-axis <axis> <options>...\nKnown options: disabled, axis, axis-inverse, pressure0-\n"
		"pressure0+, pressure-0, pressure-+, pressure+0, pressure+-\nminus=<val>, zero=<val>, plus=<val>\n"
		"tolerance=<val>\n",
		[](const std::string& t) throw(std::bad_alloc, std::runtime_error) {
			auto r = regex("([^ \t]+)[ \t]+([^ \t].*)", t, "Axis name and option(s) required");
			if(!keygroups().count(r[1]))
				throw std::runtime_error("Unknown axis name");
			auto p = keygroups()[r[1]]->get_parameters();
			switch(p.ktype) {
			case keygroup::KT_DISABLED:
			case keygroup::KT_AXIS_PAIR:
			case keygroup::KT_AXIS_PAIR_INVERSE:
			case keygroup::KT_PRESSURE_0M:
			case keygroup::KT_PRESSURE_0P:
			case keygroup::KT_PRESSURE_M0:
			case keygroup::KT_PRESSURE_MP:
			case keygroup::KT_PRESSURE_P0:
			case keygroup::KT_PRESSURE_PM:
				break;
			default:
				throw std::runtime_error("Not an axis");
			}
			regex_results r2;
			std::string options = r[2];
			unsigned found_mask = 0;
			while(options != "") {
				std::string option;
				extract_token(options, option, " \t", true);
				unsigned optmask = 0;
				if(option == "disabled") {
					p.ktype = keygroup::KT_DISABLED;
					optmask = 1;
				} else if(option == "axis") {
					p.ktype = keygroup::KT_AXIS_PAIR;
					optmask = 1;
				} else if(option == "axis-inverse") {
					p.ktype = keygroup::KT_AXIS_PAIR_INVERSE;
					optmask = 1;
				} else if(option == "pressure0-") {
					p.ktype = keygroup::KT_PRESSURE_0M;
					optmask = 1;
				} else if(option == "pressure0+") {
					p.ktype = keygroup::KT_PRESSURE_0P;
					optmask = 1;
				} else if(option == "pressure-0") {
					p.ktype = keygroup::KT_PRESSURE_M0;
					optmask = 1;
				} else if(option == "pressure-+") {
					p.ktype = keygroup::KT_PRESSURE_MP;
					optmask = 1;
				} else if(option == "pressure+-") {
					p.ktype = keygroup::KT_PRESSURE_PM;
					optmask = 1;
				} else if(option == "pressure+0") {
					p.ktype = keygroup::KT_PRESSURE_P0;
					optmask = 1;
				} else if(r2 = regex("minus=([+-]?[0-9]+)", option)) {
					p.cal_left = parse_value<int16_t>(r2[1]);
					optmask = 2;
				} else if(r2 = regex("zero=([+-]?[0-9]+)", option)) {
					p.cal_center = parse_value<int16_t>(r2[1]);
					optmask = 4;
				} else if(r2 = regex("plus=([+-]?[0-9]+)", option)) {
					p.cal_right = parse_value<int16_t>(r2[1]);
					optmask = 8;
				} else if(r2 = regex("tolerance=0?(\\.[0-9]+)", option)) {
					p.cal_tolerance = parse_value<double>(r2[1]);
					optmask = 16;
				} else
					throw std::runtime_error("Unknown axis option");
				if(found_mask & optmask)
					throw std::runtime_error("Conflicting axis modes");
				found_mask |= optmask;
			}
			if(found_mask & 1)
				keygroups()[r[1]]->change_type(p.ktype);
			keygroups()[r[1]]->change_calibration(p.cal_left, p.cal_center, p.cal_right, p.cal_tolerance);
		});

	function_ptr_command<> set_axismode("show-axes", "Show all joystick axes",
		"Syntax: show-axes\n",
		[]() throw(std::bad_alloc, std::runtime_error) {
			for(auto i = keygroups().begin(); i != keygroups().end(); ++i) {
				keygroup::parameters p = i->second->get_parameters();
				std::string type = "";
				switch(p.ktype) {
				case keygroup::KT_DISABLED: type = "disabled"; break;
				case keygroup::KT_AXIS_PAIR: type = "axis"; break;
				case keygroup::KT_AXIS_PAIR_INVERSE: type = "axis-inverse"; break;
				case keygroup::KT_PRESSURE_0M: type = "pressure0-"; break;
				case keygroup::KT_PRESSURE_0P: type = "pressure0+"; break;
				case keygroup::KT_PRESSURE_M0: type = "pressure-0"; break;
				case keygroup::KT_PRESSURE_MP: type = "pressure-+"; break;
				case keygroup::KT_PRESSURE_P0: type = "pressure+0"; break;
				case keygroup::KT_PRESSURE_PM: type = "pressure+-"; break;
				default: continue;
				}
				messages << i->first << " " << type << " -:" << p.cal_left << " 0:"
					<< p.cal_center << " +:" << p.cal_right << " t:" << p.cal_tolerance
					<< std::endl;
			}
		});


	struct triple
	{
		triple(const std::string& _a, const std::string& _b, const std::string& _c)
		{
			a = _a;
			b = _b;
			c = _c;
		}
		std::string a;
		std::string b;
		std::string c;
		bool operator==(const triple& t) const
		{
			bool x = (a == t.a && b == t.b && c == t.c);
			return x;
		}
		bool operator<(const triple& t) const
		{
			bool x = (a < t.a || (a == t.a && b < t.b) || (a == t.a && b == t.b && c < t.c));
			return x;
		}
	};
	struct keybind_data : public information_dispatch
	{
		modifier_set mod;
		modifier_set modmask;
		keygroup* group;
		unsigned subkey;
		std::string command;

		keybind_data() : information_dispatch("keybind-listener") {}

		void on_key_event(const modifier_set& modifiers, keygroup& keygroup, unsigned _subkey, bool polarity,
			const std::string& name)
		{
			if(!modifier_set::triggers(modifiers, mod, modmask))
				return;
			if(subkey != _subkey)
				return;
			if(&keygroup != group)
				return;
			std::string cmd = fixup_command_polarity(command, polarity);
			if(cmd == "")
				return;
			command::invokeC(cmd);
		}
	};

	triple parse_to_triple(const std::string& keyspec)
	{
		triple k("", "", "");
		std::string _keyspec = keyspec;
		size_t split1 = _keyspec.find_first_of("/");
		size_t split2 = _keyspec.find_first_of("|");
		if(split1 >= keyspec.length() || split2 >= keyspec.length() || split1 > split2)
			throw std::runtime_error("Bad keyspec " + keyspec);
		k.a = _keyspec.substr(0, split1);
		k.b = _keyspec.substr(split1 + 1, split2 - split1 - 1);
		k.c = _keyspec.substr(split2 + 1);
		return k;
	}

	std::map<triple, keybind_data*>& keybindings()
	{
		kmlock_hold lck;
		static std::map<triple, keybind_data*> x;
		return x;
	}
}

void keymapper::bind(std::string mod, std::string modmask, std::string keyname, std::string command)
	throw(std::bad_alloc, std::runtime_error)
{
	{
		kmlock_hold lck;
		triple k(mod, modmask, keyname);
		modifier_set _mod = modifier_set::construct(mod);
		modifier_set _modmask = modifier_set::construct(modmask);
		if(!modifier_set::valid(_mod, _modmask))
			throw std::runtime_error("Invalid modifiers");
		auto g = keygroup::lookup(keyname);
		if(!keybindings().count(k)) {
			keybindings()[k] = new keybind_data;
			keybindings()[k]->mod = _mod;
			keybindings()[k]->modmask = _modmask;
			keybindings()[k]->group = g.first;
			keybindings()[k]->subkey = g.second;
		}
		keybindings()[k]->command = command;
	}
	inverse_key::notify_update(mod + "/" + modmask + "|" + keyname, command);
}
void keymapper::unbind(std::string mod, std::string modmask, std::string keyname) throw(std::bad_alloc,
		std::runtime_error)
{
	{
		kmlock_hold lck;
		triple k(mod, modmask, keyname);
		if(!keybindings().count(k))
			throw std::runtime_error("Key is not bound");
		delete keybindings()[k];
		keybindings().erase(k);
	}
	inverse_key::notify_update(mod + "/" + modmask + "|" + keyname, "");
}

void keymapper::dumpbindings() throw(std::bad_alloc)
{
	kmlock_hold lck;
	for(auto i : keybindings()) {
		messages << "bind-key ";
		if(i.first.a != "" || i.first.b != "")
			messages << i.first.a << "/" << i.first.b << " ";
		messages << i.first.c << " " << i.second->command << std::endl;
	}
}

std::set<std::string> keymapper::get_bindings() throw(std::bad_alloc)
{
	kmlock_hold lck;
	std::set<std::string> r;
	for(auto i : keybindings())
		r.insert(i.first.a + "/" + i.first.b + "|" + i.first.c);
	return r;
}

std::string keymapper::get_command_for(const std::string& keyspec) throw(std::bad_alloc)
{
	kmlock_hold lck;
	triple k("", "", "");
	try {
		k = parse_to_triple(keyspec);
	} catch(std::exception& e) {
		return "";
	}
	if(!keybindings().count(k))
		return "";
	return keybindings()[k]->command;
}

void keymapper::bind_for(const std::string& keyspec, const std::string& cmd) throw(std::bad_alloc, std::runtime_error)
{
	triple k("", "", "");
	k = parse_to_triple(keyspec);
	if(cmd != "")
		bind(k.a, k.b, k.c, cmd);
	else
		unbind(k.a, k.b, k.c);
}


inverse_key::inverse_key(const std::string& command, const std::string& name) throw(std::bad_alloc)
{
	kmlock_hold lck;
	cmd = command;
	oname = name;
	ikeys().insert(this);
	forkey()[cmd] = this;
	//Search the keybindings for matches.
	auto b = keymapper::get_bindings();
	for(auto c : b)
		if(keymapper::get_command_for(c) == cmd)
			addkey(c);
}

inverse_key::~inverse_key()
{
	kmlock_hold lck;
	ikeys().erase(this);
	forkey().erase(cmd);
}

std::set<inverse_key*> inverse_key::get_ikeys() throw(std::bad_alloc)
{
	kmlock_hold lck;
	return ikeys();
}

std::string inverse_key::getname() throw(std::bad_alloc)
{
	kmlock_hold lck;
	return oname;
}

inverse_key* inverse_key::get_for(const std::string& command) throw(std::bad_alloc)
{
	kmlock_hold lck;
	return forkey().count(command) ? forkey()[command] : NULL;
}

std::set<inverse_key*>& inverse_key::ikeys()
{
	kmlock_hold lck;
	static std::set<inverse_key*> x;
	return x;
}

std::map<std::string, inverse_key*>& inverse_key::forkey()
{
	kmlock_hold lck;
	static std::map<std::string, inverse_key*> x;
	return x;
}

std::string inverse_key::get(bool primary) throw(std::bad_alloc)
{
	kmlock_hold lck;
	return primary ? primary_spec : secondary_spec;
}

void inverse_key::clear(bool primary) throw(std::bad_alloc)
{
	kmlock_hold lck;
	if(primary) {
		if(primary_spec != "")
			keymapper::bind_for(primary_spec, "");
		primary_spec = secondary_spec;
		secondary_spec = "";
	} else {
		if(secondary_spec != "")
			keymapper::bind_for(secondary_spec, "");
		secondary_spec = "";
	}
	//Search the keybindings for matches.
	auto b = keymapper::get_bindings();
	for(auto c : b)
		if(keymapper::get_command_for(c) == cmd)
			addkey(c);
}

void inverse_key::set(std::string keyspec, bool primary) throw(std::bad_alloc)
{
	kmlock_hold lck;
	if(keyspec == "") {
		clear(primary);
		return;
	}
	if(!primary && (primary_spec == "" || primary_spec == keyspec))
		primary = true;
	if(primary) {
		if(primary_spec != "")
			keymapper::bind_for(primary_spec, "");
		primary_spec = keyspec;
		keymapper::bind_for(primary_spec, cmd);
	} else {
		if(secondary_spec != "")
			keymapper::bind_for(secondary_spec, "");
		secondary_spec = keyspec;
		keymapper::bind_for(secondary_spec, cmd);
	}
}

void inverse_key::addkey(const std::string& keyspec)
{
	kmlock_hold lck;
	if(primary_spec == "" || primary_spec == keyspec)
		primary_spec = keyspec;
	else if(secondary_spec == "")
		secondary_spec = keyspec;
}

void inverse_key::notify_update(const std::string& keyspec, const std::string& command)
{
	kmlock_hold lck;
	for(auto k : ikeys()) {
		bool u = false;
		if(k->primary_spec == keyspec || k->secondary_spec == keyspec) {
			if(command == "" || command != k->cmd) {
				//Unbound.
				k->primary_spec = "";
				k->secondary_spec = "";
				u = true;
			}
		} else if(command == k->cmd)
			k->addkey(keyspec);
		if(u) {
			auto b = keymapper::get_bindings();
			for(auto c : b)
				if(keymapper::get_command_for(c) == k->cmd)
					k->addkey(c);
		}
	}
}
