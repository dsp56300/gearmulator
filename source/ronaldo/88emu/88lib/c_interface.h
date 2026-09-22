/*
 * C interface of 88lib.
 *
 * For embedding 88emu in a host that is not C++, or that does not want the C++ headers:
 * a game emulator's MIDI back end, a language binding, a WebAssembly bundle.
 * The shape follows the C interface of Munt's mt32emu on purpose (a context that ROMs are
 * offered to, open_synth / close_synth, play_msg / play_sysex / parse_stream, render_bit16s /
 * render_float), so a host that already drives mt32emu drives this the same way.
 *
 * What differs, because these are emulations of whole devices rather than of a synth engine:
 *   - ROMs are found on search paths and identified by content, never by name
 *     (emu88_add_rom_path). A context selects which device to build (emu88_select_device).
 *   - A device boots. emu88_open_synth() runs the firmware offline as far as the boot flags
 *     ask, which takes emulated seconds and real time; afterwards it takes MIDI at once.
 *   - A device can have several MIDI inputs (SC-88 family: two; SC-8850: four). Every MIDI
 *     function has an _on_port form; the plain forms address input 0.
 *
 * Threading: a context is not thread-safe; use it from one thread at a time. The ROM search
 * paths are shared by the whole process.
 */
#ifndef EMU88_C_INTERFACE_H
#define EMU88_C_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

#if defined(EMU88_SHARED) && defined(_WIN32)
#	ifdef EMU88_EXPORTS
#		define EMU88_EXPORT __declspec(dllexport)
#	else
#		define EMU88_EXPORT __declspec(dllimport)
#	endif
#elif defined(__GNUC__) || defined(__clang__)
#	define EMU88_EXPORT __attribute__((visibility("default")))
#else
#	define EMU88_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emu88_data* emu88_context;

typedef enum
{
	/* Operation completed normally. */
	EMU88_RC_OK = 0,

	/* Definite error occurred. */
	EMU88_RC_PATH_NOT_FOUND = -2,
	EMU88_RC_MISSING_ROMS = -4,
	EMU88_RC_NOT_OPENED = -5,
	EMU88_RC_UNKNOWN_DEVICE = -7,
	EMU88_RC_INVALID_ARGUMENT = -8,

	/* Undefined error occurred. */
	EMU88_RC_FAILED = -100
} emu88_return_code;

/* A device model. The values are those of emu88Lib::DeviceModel and are stable. */
typedef int emu88_device_id;

enum
{
	EMU88_DEVICE_SC88 = 0,
	EMU88_DEVICE_SC88VL = 1,
	EMU88_DEVICE_SC88PRO = 2,
	EMU88_DEVICE_SC8850 = 3,
	EMU88_DEVICE_SC55MK2 = 4,
	EMU88_DEVICE_SC55 = 5,
	EMU88_DEVICE_SC55ST = 6,
	EMU88_DEVICE_CM300 = 7,
	EMU88_DEVICE_SCB55 = 8,
	EMU88_DEVICE_RLP3237 = 9,
	EMU88_DEVICE_SC155 = 10,
	EMU88_DEVICE_SC155MK2 = 11,
	EMU88_DEVICE_XPGS = 12,
	EMU88_DEVICE_SC8820 = 13,
	EMU88_DEVICE_CM32P = 14,
	EMU88_DEVICE_VEGSPRO = 15,
	EMU88_DEVICE_SCC1A = 16,
	EMU88_DEVICE_CM64 = 17,
	EMU88_DEVICE_CM32L = 18,
	EMU88_DEVICE_NU10B = 19,
	EMU88_DEVICE_MIIG5 = 20,
	/* The two MT-32 boards: the old type runs the 1.x firmware, the new type the 2.x. */
	EMU88_DEVICE_MT32_OLD = 21,
	EMU88_DEVICE_MT32_NEW = 22,
	/* The CM-32LN, the CM-500's LA half and the LAPC-N. */
	EMU88_DEVICE_CM32LN = 23
	/* Enumerate them in presentation order with emu88_get_device_count() / emu88_get_device_id(). */
};

/* The MT-32's front-panel switches, as bits of emu88_set_panel_buttons(). The other boards number
 * theirs as their emu88Lib board headers do. */
enum
{
	EMU88_MT32_BUTTON_PART1 = 1 << 0,
	EMU88_MT32_BUTTON_PART2 = 1 << 1,
	EMU88_MT32_BUTTON_PART3 = 1 << 2,
	EMU88_MT32_BUTTON_SOUND_GROUP = 1 << 3,
	EMU88_MT32_BUTTON_VOLUME = 1 << 4,
	EMU88_MT32_BUTTON_PART4 = 1 << 8,
	EMU88_MT32_BUTTON_PART5 = 1 << 9,
	EMU88_MT32_BUTTON_RHYTHM = 1 << 10,
	EMU88_MT32_BUTTON_SOUND = 1 << 11,
	EMU88_MT32_BUTTON_MASTER_VOLUME = 1 << 12
};

/* What emu88_open_synth() does before it returns. */
enum
{
	/* Run the firmware's own factory initialization first, on the boards that have one. */
	EMU88_BOOT_FACTORY_RESET = 1,
	/* Keep running until the power-on intro is over, so the device takes MIDI at once. */
	EMU88_BOOT_SKIP_INTRO = 2,
	EMU88_BOOT_DEFAULT = EMU88_BOOT_FACTORY_RESET | EMU88_BOOT_SKIP_INTRO
};

/* == Library == */

EMU88_EXPORT const char* emu88_get_library_version_string(void);

/* == Devices and ROMs (process-wide) == */

/* Adds a folder to the ROM search paths, searched with its subfolders, and sweeps the paths
 * again. Images are identified by content; standardized filenames are accepted too. */
EMU88_EXPORT emu88_return_code emu88_add_rom_path(const char* path);
/* Replaces every search path, the built-in defaults included, with this one folder. */
EMU88_EXPORT emu88_return_code emu88_set_rom_path(const char* path);
/* Sweeps the search paths again, e.g. after the user added a dump. */
EMU88_EXPORT void emu88_rescan_roms(void);

/* The devices this build knows, in presentation order. */
EMU88_EXPORT int emu88_get_device_count(void);
EMU88_EXPORT emu88_device_id emu88_get_device_id(int index);
/* Display name, or NULL for an unknown id. */
EMU88_EXPORT const char* emu88_get_device_name(emu88_device_id device);
/* Number of MIDI inputs the device tells apart; 0 for an unknown id. */
EMU88_EXPORT int emu88_get_device_midi_port_count(emu88_device_id device);
/* The lowest MIDI channel the device answers on from power-on, 0-based: 1 on the MT-32 and CM-32L
 * boards (part 1; the rhythm part is on 9), 10 on the CM-32P, 0 everywhere else. */
EMU88_EXPORT int emu88_get_device_first_midi_channel(emu88_device_id device);
/* Whether the device has a slot for an SN-U110 series PCM card (the CM-32P and the CM-64). */
EMU88_EXPORT int emu88_device_has_pcm_card_slot(emu88_device_id device);
/* Whether every image the device needs was found on the search paths. */
EMU88_EXPORT int emu88_is_device_available(emu88_device_id device);
/* Describes the images the device needs, as text for a user. Copies at most buffer_size bytes
 * including the terminator and returns the length of the whole text, as snprintf does. */
EMU88_EXPORT size_t emu88_describe_device_roms(emu88_device_id device, char* buffer, size_t buffer_size);

/* == Context == */

EMU88_EXPORT emu88_context emu88_create_context(void);
/* Closes the synth if it is open and releases the context. NULL is accepted. */
EMU88_EXPORT void emu88_free_context(emu88_context context);

/* The following take effect at the next emu88_open_synth(). */
EMU88_EXPORT emu88_return_code emu88_select_device(emu88_context context, emu88_device_id device);
/* A combination of EMU88_BOOT_*; EMU88_BOOT_DEFAULT unless set. */
EMU88_EXPORT void emu88_set_boot_flags(emu88_context context, unsigned flags);
/* The PCM card in the slot of a device that has one, as a raw SN-U110 series card image of up to
 * 512 KiB in either of the two dump byte orders; the image is copied. NULL or 0 empties the slot.
 * Fails with EMU88_RC_INVALID_ARGUMENT when the image is not readable as a card. */
EMU88_EXPORT emu88_return_code emu88_set_pcm_card(emu88_context context, const uint8_t* image, size_t length);
/* The rate emu88_render_*() produce. 0 (the default) is the device's own DAC rate, without
 * conversion. May also be changed while the synth is open. */
EMU88_EXPORT void emu88_set_stereo_output_samplerate(emu88_context context, double samplerate);

/* Builds and boots the selected device. Blocks while the firmware runs (see EMU88_BOOT_*). */
EMU88_EXPORT emu88_return_code emu88_open_synth(emu88_context context);
EMU88_EXPORT void emu88_close_synth(emu88_context context);
EMU88_EXPORT int emu88_is_open(emu88_context context);
/* The rate emu88_render_*() produce right now; 0 while closed. */
EMU88_EXPORT uint32_t emu88_get_actual_stereo_output_samplerate(emu88_context context);
/* The rate of the device's DAC; 0 while closed. */
EMU88_EXPORT uint32_t emu88_get_device_samplerate(emu88_context context);
EMU88_EXPORT int emu88_get_midi_port_count(emu88_context context);

/* == MIDI ==
 * Everything played is delivered with the next frame rendered. */

/* A short message packed as in mt32emu and the Windows MME API: status | data1 << 8 | data2 << 16. */
EMU88_EXPORT emu88_return_code emu88_play_msg(emu88_context context, uint32_t msg);
EMU88_EXPORT emu88_return_code emu88_play_msg_on_port(emu88_context context, unsigned port, uint32_t msg);
/* One complete SysEx, F0 ... F7. */
EMU88_EXPORT emu88_return_code emu88_play_sysex(emu88_context context, const uint8_t* sysex, uint32_t length);
EMU88_EXPORT emu88_return_code emu88_play_sysex_on_port(emu88_context context, unsigned port, const uint8_t* sysex, uint32_t length);
/* Raw wire bytes. Running status is honoured, and a message may span calls. */
EMU88_EXPORT emu88_return_code emu88_parse_stream(emu88_context context, const uint8_t* stream, uint32_t length);
EMU88_EXPORT emu88_return_code emu88_parse_stream_on_port(emu88_context context, unsigned port, const uint8_t* stream, uint32_t length);
/* The device's own reset, on every input: GS Reset on the Sound Canvas family and the modules
 * built on it, and the "all parameters reset" data set to address 7F 00 00 on the MT-32, CM-32L,
 * CM-32P and CM-64, which know no other. The MT-32 family takes a moment to come back. */
EMU88_EXPORT emu88_return_code emu88_play_device_reset(emu88_context context);
/* The messages that silence a channel on this device, as a host stops or seeks: All Sound Off,
 * or hold pedal up and All Notes Off on the MT-32 and CM boards, whose firmware predates All Sound
 * Off. Sent to every channel of every input. */
EMU88_EXPORT emu88_return_code emu88_play_silence(emu88_context context);

/* == Front panel ==
 * The panel is read by the firmware at its own pace; a press has to last long enough to be seen,
 * which a few milliseconds of rendering are. */

/* The switches held down, as a bit per switch in the board's own numbering (EMU88_MT32_BUTTON_*
 * on the MT-32). 0 releases them all. Boards without a panel ignore it. */
EMU88_EXPORT emu88_return_code emu88_set_panel_buttons(emu88_context context, uint32_t buttons);
/* Turns the SC-8850's VALUE encoder by detents, or the MT-32's VOLUME/VALUE knob by 32nds of its
 * travel; negative is anticlockwise. */
EMU88_EXPORT emu88_return_code emu88_turn_panel_encoder(emu88_context context, int detents);
/* The front-panel lamps, a bit per lamp in the board's numbering; bit 0 is MIDI MESSAGE on the
 * MT-32 and CM boards. 0 while closed. */
EMU88_EXPORT uint32_t emu88_get_panel_leds(emu88_context context);
/* The text on a character display as the firmware last left it, lines separated by '\n', in the
 * controller's character set (ASCII for the printable range). screen 1 is the CM-64's second
 * display, the LA half's, below the CM-32P's. Copies at most buffer_size bytes including the
 * terminator and returns the length of the whole text, as snprintf does; 0 while closed, when
 * the screen does not exist, or on a graphic display like the SC-8850's. The MT-32's display
 * is 20 characters on one line. */
EMU88_EXPORT size_t emu88_get_display_text(emu88_context context, unsigned screen, char* buffer, size_t buffer_size);
/* Whether that display is switched on, 1 or 0. */
EMU88_EXPORT int emu88_is_display_on(emu88_context context, unsigned screen);

/* == Audio ==
 * `length` counts stereo frames; `stream` takes 2 * length interleaved samples. Rendering a
 * closed synth produces silence. */
EMU88_EXPORT void emu88_render_bit16s(emu88_context context, int16_t* stream, uint32_t length);
EMU88_EXPORT void emu88_render_float(emu88_context context, float* stream, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* EMU88_C_INTERFACE_H */
