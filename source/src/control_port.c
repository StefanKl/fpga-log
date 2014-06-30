/**
 * @file control_port.c
 * @brief control_port implementations to realize control interface to device drivers
 *
 * @author Carsten Bruns (bruns@lichttechnik.tu-darmstadt.de)
 */

#include <fpga-log/control_port.h>

/**
 * @brief control_port measure dummy function
 *
 * @param parent	not used
 */
static void control_port_new_control_message_dummy(void* const parent,
		unsigned int count, const control_parameter_t* parameters) {
}

const control_port_t control_port_dummy = { 0,
		control_port_new_control_message_dummy };
