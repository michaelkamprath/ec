// SPDX-License-Identifier: GPL-3.0-only

#include <board/fan.h>
#include <board/gpio.h>
#include <board/peci.h>
#include <board/power.h>
#include <common/debug.h>
#include <common/macro.h>
#include <ec/espi.h>
#include <ec/gpio.h>
#include <ec/pwm.h>

#ifndef USE_S0IX
    #define USE_S0IX 0
#endif

// Fan speed is the lowest requested over HEATUP seconds
#ifndef BOARD_HEATUP
    #define BOARD_HEATUP 4
#endif

static uint8_t FAN_HEATUP[BOARD_HEATUP] = { 0 };

// Fan speed is the highest HEATUP speed over COOLDOWN seconds
#ifndef BOARD_COOLDOWN
    #define BOARD_COOLDOWN 10
#endif

static uint8_t FAN_COOLDOWN[BOARD_COOLDOWN] = { 0 };

// Tjunction = 100C for i7-8565U (and probably the same for all WHL-U)
#define T_JUNCTION 100

bool peci_on = false;
int16_t peci_temp = 0;

#define PECI_TEMP(X) ((int16_t)(X))

#define FAN_POINT(T, D) { .temp = PECI_TEMP(T), .duty = PWM_DUTY(D) }

// Fan curve with temperature in degrees C, duty cycle in percent
static struct FanPoint __code FAN_POINTS[] = {
#ifdef BOARD_FAN_POINTS
    BOARD_FAN_POINTS
#else
    FAN_POINT(70, 40),
    FAN_POINT(75, 50),
    FAN_POINT(80, 60),
    FAN_POINT(85, 65),
    FAN_POINT(90, 65)
#endif
};

static struct Fan __code FAN = {
    .points = FAN_POINTS,
    .points_size = ARRAY_SIZE(FAN_POINTS),
    .heatup = FAN_HEATUP,
    .heatup_size = ARRAY_SIZE(FAN_HEATUP),
    .cooldown = FAN_COOLDOWN,
    .cooldown_size = ARRAY_SIZE(FAN_COOLDOWN),
    .interpolate = SMOOTH_FANS != 0,
};

void peci_init(void) {
    // Allow PECI pin to be used
    GCR2 |= BIT(4);

    // Set frequency to 1MHz
    HOCTL2R = 0x01;
    // Set VTT to 1V
    PADCTLR = 0x02;
}

// Returns positive completion code on success, negative completion code or
// negative (0x1000 | status register) on PECI hardware error
int16_t peci_wr_pkg_config(uint8_t index, uint16_t param, uint32_t data) {
    // Wait for completion
    while (HOSTAR & 1) {}
    // Clear status
    HOSTAR = HOSTAR;

    // Enable PECI, clearing data fifo's, enable AW_FCS
    HOCTLR = BIT(5) | BIT(3) | BIT(1);
    // Set address to default
    HOTRADDR = 0x30;
    // Set write length
    HOWRLR = 10;
    // Set read length
    HORDLR = 1;
    // Set command
    HOCMDR = 0xA5;

    // Write host ID
    HOWRDR = 0;
    // Write index
    HOWRDR = index;
    // Write param
    HOWRDR = (uint8_t)param;
    HOWRDR = (uint8_t)(param >> 8);
    // Write data
    HOWRDR = (uint8_t)data;
    HOWRDR = (uint8_t)(data >> 8);
    HOWRDR = (uint8_t)(data >> 16);
    HOWRDR = (uint8_t)(data >> 24);

    // Start transaction
    HOCTLR |= 1;

    // Wait for completion
    while (HOSTAR & 1) {}

    int16_t status = (int16_t)HOSTAR;
    if (status & BIT(1)) {
        int16_t cc = (int16_t)HORDDR;
        if (cc & 0x80) {
            return -cc;
        } else {
            return cc;
        }
    } else {
        return -(0x1000 | status);
    }
}

// PECI information can be found here: https://www.intel.com/content/dam/www/public/us/en/documents/design-guides/core-i7-lga-2011-guide.pdf
uint8_t peci_get_fan_duty(void) {
    uint8_t duty;

#if USE_S0IX
    // Use PECI if platform is not in CS
    peci_on = gpio_get(&SLP_S0_N);
#else // USE_S0IX
    // Use PECI if in S0 state
    peci_on = power_state == POWER_STATE_S0;
#endif // USE_S0IX

    if (peci_on) {
        // Wait for completion
        while (HOSTAR & 1) {}
        // Clear status
        HOSTAR = HOSTAR;

        // Enable PECI, clearing data fifo's
        HOCTLR = BIT(5) | BIT(3);
        // Set address to default
        HOTRADDR = 0x30;
        // Set write length
        HOWRLR = 1;
        // Set read length
        HORDLR = 2;
        // Set command
        HOCMDR = 1;
        // Start transaction
        HOCTLR |= 1;

        // Wait for completion
        while (HOSTAR & 1) {}

        if (HOSTAR & BIT(1)) {
            // Use result if finished successfully
            uint8_t low = HORDDR;
            uint8_t high = HORDDR;
            uint16_t peci_offset = (((int16_t)high << 8) | (int16_t)low) >> 6;

            peci_temp = PECI_TEMP(T_JUNCTION) + peci_offset;
            duty = fan_duty(&FAN, peci_temp);
        } else {
            // Default to 50% if there is an error
            peci_temp = 0;
            duty = PWM_DUTY(50);
        }
    } else {
        // Turn fan off if not in S0 state
        peci_temp = 0;
        duty = PWM_DUTY(0);
    }

    if (peci_on && fan_max) {
        // Override duty if fans are manually set to maximum
        duty = PWM_DUTY(100);
    } else {
        // Apply heatup and cooldown filters to duty
        duty = fan_heatup(&FAN, duty);
        duty = fan_cooldown(&FAN, duty);
    }

    TRACE("PECI temp=%d\n", peci_temp);
    return duty;
}
