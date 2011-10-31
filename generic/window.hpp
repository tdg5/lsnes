#ifndef _window__hpp__included__
#define _window__hpp__included__

#include "render.hpp"
#include <string>
#include <map>
#include <list>
#include <stdexcept>

#define WINSTATE_NORMAL 0
#define WINSTATE_COMMAND 1
#define WINSTATE_MODAL 2
#define WINSTATE_IDENTIFY 3

class window;

/**
 * Some backnotifications.
 */
class window_callback
{
public:
	virtual ~window_callback() throw();
/**
 * Called when user tries to close the window.
 */
	virtual void on_close() throw();
/**
 * Called when user clicks on the screen.
 */
	virtual void on_click(int32_t x, int32_t y, uint32_t buttonmask) throw();
/**
 * Do try to close the window.
 */
	static void do_close() throw();
/**
 * Do click on the screen.
 */
	static void do_click(int32_t x, int32_t y, uint32_t buttonmask) throw();
/**
 * Set the callback handler.
 */
	static void set_callback_handler(window_callback& cb) throw();
};

/**
 * Sound/Graphics init/quit functions. Sound init is called after graphics init, and vice versa for quit.
 *
 * These need to be implemented by the corresponding plugins.
 */
void graphics_init();
void sound_init();
void sound_quit();
void graphics_quit();
void joystick_init();
void joystick_quit();

/**
 * This is a handle to graphics system. Note that creating multiple contexts produces undefined results.
 */
class window
{
public:
/**
 * Window constructor.
 */
	window() throw() {}

/**
 * Initialize the graphics system.
 *
 * Implemented by generic window code.
 */
	static void init();

/**
 * Shut down the graphics system.
 *
 * Implemented by generic window code.
 */
	static void quit();

/**
 * Get output stream printing into message queue.
 *
 * Note that lines printed there should be terminated by '\n'.
 *
 * Implemented by the generic window code.
 *
 * returns: The output stream.
 * throws std::bad_alloc: Not enough memory.
 */
	static std::ostream& out() throw(std::bad_alloc);

/**
 * Get emulator status area
 *
 * Implemented by the generic window code.
 * 
 * returns: Emulator status area.
 */
	static std::map<std::string, std::string>& get_emustatus() throw();

/**
 * Adds a messages to mesage queue to be shown.
 *
 * Needs to be implemented by the graphics plugin.
 *
 * parameter msg: The messages to add (split by '\n').
 * throws std::bad_alloc: Not enough memory.
 */
	static void message(const std::string& msg) throw(std::bad_alloc);

/**
 * Displays a modal message, not returning until the message is acknowledged. Keybindings are not available, but
 * should quit be generated somehow, modal message will be closed and command callback triggered.
 *
 * Needs to be implemented by the graphics plugin.
 *
 * parameter msg: The message to show.
 * parameter confirm: If true, ask for Ok/cancel type input.
 * returns: If confirm is true, true if ok was chosen, false if cancel was chosen. Otherwise always false.
 * throws std::bad_alloc: Not enough memory.
 */
	static bool modal_message(const std::string& msg, bool confirm = false) throw(std::bad_alloc);

/**
 * Displays fatal error message, quitting after the user acks it.
 *
 * Needs to be implemented by the graphics plugin.
 */
	static void fatal_error() throw();

/**
 * Processes inputs. If in non-modal mode (normal mode without pause), this returns quickly. Otherwise it waits
 * for modal mode to exit. Also needs to call poll_joysticks().
 *
 * Needs to be implemented by the graphics plugin.
 * 
 * throws std::bad_alloc: Not enough memory.
 */
	static void poll_inputs() throw(std::bad_alloc);

/**
 * Notify that the screen has been updated.
 *
 * Needs to be implemented by the graphics plugin.
 *
 * parameter full: Do full refresh if true.
 */
	static void notify_screen_update(bool full = false) throw();

/**
 * Set the screen to use as main surface.
 *
 * Needs to be implemented by the graphics plugin.
 *
 * parameter scr: The screen to use.
 */
	static void set_main_surface(screen& scr) throw();

/**
 * Enable/Disable pause mode.
 *
 * Needs to be implemented by the graphics plugin.
 *
 * parameter enable: Enable pause if true, disable otherwise.
 */
	static void paused(bool enable) throw();

/**
 * Wait specified number of microseconds (polling for input).
 *
 * Needs to be implemented by the graphics plugin.
 * 
 * parameter usec: Number of us to wait.
 * throws std::bad_alloc: Not enough memory.
 */
	static void wait_usec(uint64_t usec) throw(std::bad_alloc);

/**
 * Cancel pending wait_usec, making it return now.
 *
 * Needs to be implemented by the graphics plugin.
 */
	static void cancel_wait() throw();

/**
 * Set window main screen compensation parameters. This is used for mouse click reporting.
 *
 * Needs to be implemented by the graphics plugin.
 *
 * parameter xoffset: X coordinate of origin.
 * parameter yoffset: Y coordinate of origin.
 * parameter hscl: Horizontal scaling factor.
 * parameter vscl: Vertical scaling factor.
 */
	static void set_window_compensation(uint32_t xoffset, uint32_t yoffset, uint32_t hscl, uint32_t vscl);

/**
 * Enable or disable sound.
 *
 * Needs to be implemented by the sound plugin. 
 *
 * parameter enable: Enable sounds if true, otherwise disable sounds.
 */
	static void sound_enable(bool enable) throw();

/**
 * Input audio sample (at specified rate).
 *
 * Needs to be implemented by the sound plugin. 
 *
 * parameter left: Left sample.
 * parameter right: Right sample.
 */
	static void play_audio_sample(uint16_t left, uint16_t right) throw();

/**
 * Set sound sampling rate.
 *
 * Needs to be implemented by the sound plugin. 
 *
 * parameter rate_n: Numerator of sampling rate.
 * parameter rate_d: Denomerator of sampling rate.
 */
	static void set_sound_rate(uint32_t rate_n, uint32_t rate_d);

/**
 * Has the sound system been successfully initialized?
 *
 * Needs to be implemented by the sound plugin. 
 */
	static bool sound_initialized();

/**
 * Set sound device.
 *
 * Needs to be implemented by the sound plugin. 
 */
	static void set_sound_device(const std::string& dev);

/**
 * Get current sound device.
 *
 * Needs to be implemented by the sound plugin. 
 */
	static std::string get_current_sound_device();

/**
 * Get available sound devices.
 *
 * Needs to be implemented by the sound plugin. 
 */
	static std::map<std::string, std::string> get_sound_devices();
/**
 * Poll joysticks.
 *
 * Needs to be implemented by the joystick plugin.
 */
	static void poll_joysticks();
private:
	window(const window&);
	window& operator==(const window&);
};

/**
 * Names of plugins.
 */
extern const char* sound_plugin_name;
extern const char* graphics_plugin_name;
extern const char* joystick_plugin_name;

#endif
