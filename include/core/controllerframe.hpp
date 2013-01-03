#ifndef _controllerframe__hpp__included__
#define _controllerframe__hpp__included__

#include <cstring>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include <list>
#include "library/controller-data.hpp"

/**
 * Analog indices.
 */
#define MAX_ANALOG 3


/**
 * Controllers state.
 */
class controller_state
{
public:
/**
 * Constructor.
 */
	controller_state() throw();
/**
 * Convert lcid (Logical Controller ID) into pcid (Physical Controler ID).
 *
 * Parameter lcid: The logical controller ID.
 * Return: The physical controller ID, or <-1, -1> if no such controller exists.
 */
	std::pair<int, int> lcid_to_pcid(unsigned lcid) throw();
/**
 * Lookup (port,controller) pair corresponding to given legacy pcid.
 *
 * Parameter pcid: The legacy pcid.
 * Returns: The controller index, or <-1, -1> if no such thing exists.
 * Note: Even if this does return a valid index, it still may not exist.
 */
	std::pair<int, int> legacy_pcid_to_pair(unsigned pcid) throw();
/**
 * Convert acid (Analog Controller ID) into pcid.
 *
 * Parameter acid: The analog controller ID.
 * Return: The physical controller ID, or <-1,-1> if no such controller exists.
 */
	std::pair<int, int> acid_to_pcid(unsigned acid) throw();
/**
 * Is given acid a mouse?
 *
 * Parameter acid: The analog controller ID.
 * Returns: True if given acid is mouse, false otherwise.
 */
	bool acid_is_mouse(unsigned acid) throw();
/**
 * Is given pcid present?
 *
 * Parameter port: The port.
 * Parameter controller: The controller.
 * Returns: True if present, false if not.
 */
	bool pcid_present(unsigned port, unsigned controller) throw();
/**
 * Set types of ports.
 *
 * Parameter ptype: The new types for ports.
 * Parameter set_core: If true, set the core port types too, otherwise don't do that.
 * Throws std::runtime_error: Illegal port type.
 */
	void set_ports(const port_type_set& ptype, bool set_core) throw(std::runtime_error);
/**
 * Get status of current controls (with autohold/autofire factored in).
 *
 * Parameter framenum: Number of current frame (for evaluating autofire).
 * Returns: The current controls.
 */
	controller_frame get(uint64_t framenum) throw();
/**
 * Commit given controls (autohold/autofire is factored in).
 *
 * Parameter framenum: Number of current frame (for evaluating autofire).
 * Returns: The committed controls.
 */
	controller_frame commit(uint64_t framenum) throw();
/**
 * Commit given controls (autohold/autofire is ignored).
 *
 * Parameter controls: The controls to commit
 * Returns: The committed controls.
 */
	controller_frame commit(controller_frame controls) throw();
/**
 * Get status of committed controls.
 * Returns: The committed controls.
 */
	controller_frame get_committed() throw();
/**
 * Get blank frame.
 */
	controller_frame get_blank() throw();
/**
 * Send analog input to given controller.
 *
 * Parameter port: The port to send input to.
 * Parameter controller: The controller to send input to.
 * Parameter x: The x coordinate to send.
 * Parameter y: The x coordinate to send.
 */
	void analog(unsigned port, unsigned controller, int x, int y) throw();
/**
 * Manipulate autohold.
 *
 * Parameter port: The port.
 * Parameter controller: The controller.
 * Parameter pbid: The physical button ID to manipulate.
 * Parameter newstate: The new state for autohold.
 */
	void autohold2(unsigned port, unsigned controller, unsigned pbid, bool newstate) throw();
/**
 * Query autohold.
 *
 * Parameter port: The port.
 * Parameter controller: The controller.
 * Parameter pbid: The physical button ID to query.
 * Returns: The state of autohold.
 */
	bool autohold2(unsigned port, unsigned controller, unsigned pbid) throw();
/**
 * Reset all frame holds.
 */
	void reset_framehold() throw();
/**
 * Manipulate hold for frame.
 *
 * Parameter port: The port.
 * Parameter controller: The controller.
 * Parameter pbid: The physical button ID to manipulate.
 * Parameter newstate: The new state for framehold.
 */
	void framehold2(unsigned port, unsigned controller, unsigned pbid, bool newstate) throw();
/**
 * Query hold for frame.
 *
 * Parameter port: The port.
 * Parameter controller: The controller.
 * Parameter pbid: The physical button ID to query.
 * Returns: The state of framehold.
 */
	bool framehold2(unsigned port, unsigned controller, unsigned pbid) throw();
/**
 * Manipulate button.
 *
 * Parameter port: The port.
 * Parameter controller: The controller.
 * Parameter pbid: The physical button ID to manipulate.
 * Parameter newstate: The new state for button.
 */
	void button2(unsigned port, unsigned controller, unsigned pbid, bool newstate) throw();
/**
 * Query button.
 *
 * Parameter port: The port.
 * Parameter controller: The controller.
 * Parameter pbid: The physical button ID to query.
 * Returns: The state of button.
 */
	bool button2(unsigned port, unsigned controller, unsigned pbid) throw();
/**
 * Set autofire pattern.
 *
 * Parameter pattern: The new pattern.
 * Throws std::bad_alloc: Not enough memory.
 */
	void autofire(std::vector<controller_frame> pattern) throw(std::bad_alloc);
/**
 * Get physical button ID for physical controller ID and logical button ID.
 *
 * Parameter port: The port.
 * Parameter controller: The controller.
 * Parameter lbid: Logical button id.
 * Returns: The physical button id, or -1 if no such button.
 */
	int button_id(unsigned port, unsigned controller, unsigned lbid) throw();
/**
 * TODO: Document.
 */
	bool is_present(unsigned port, unsigned controller) throw();
/**
 * TODO: Document.
 */
	bool is_analog(unsigned port, unsigned controller) throw();
/**
 * TODO: Document.
 */
	bool is_mouse(unsigned port, unsigned controller) throw();
private:
	const port_type_set* types;
	std::pair<int, int> analog_indices[MAX_ANALOG];
	bool analog_mouse[MAX_ANALOG];
	controller_frame _input;
	controller_frame _autohold;
	controller_frame _framehold;
	controller_frame _committed;
	std::vector<controller_frame> _autofire;
};

/**
 * Generic port controller name function.
 */
template<unsigned controllers, const char** name>
inline const char* generic_controller_name(unsigned controller)
{
	if(controller >= controllers)
		return NULL;
	return *name;
}

#endif
