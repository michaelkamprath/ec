// SPDX-License-Identifier: GPL-3.0-only

#include <board/pwm.h>
#include <common/macro.h>

void pwm_init(void) {
    // Set T0CHSEL to TACH0A and T1CHSEL to TACH1A
    TSWCTLR = 0;

    // Disable PWM
    ZTIER = 0;

    // Set prescalar clock frequency to EC clock
    PCFSR = 0b01;

    // Use C0CPRS and CTR0 for all channels
    PCSSGL = 0;
    PCSSGH = 0;

    // Set clock prescaler to 0 + 1
    C0CPRS = 0;

    // Set cycle time to 255 + 1
    CTR0 = 255;

    // Turn off CPU fan (temperature control in peci_get_fan_duty)
    DCR2 = 0;

#ifdef it5570e
    // Reload counters when they reach 0 instead of immediately
    PWMLCCR = 0xFF;
#endif

    // Enable PWM
    ZTIER = BIT(1);
}
