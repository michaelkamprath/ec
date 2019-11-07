#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <arch/delay.h>
#include <board/battery.h>
#include <board/gpio.h>
#include <board/gctrl.h>
#include <board/kbc.h>
#include <board/kbscan.h>
#include <board/pmc.h>
#include <board/ps2.h>
#include <board/pwm.h>
#include <board/smbus.h>
#include <common/macro.h>

void external_0(void) __interrupt(0) {
    printf("external_0\n");
}

void timer_0(void) __interrupt(1) {
    printf("timer_0\n");
}

void external_1(void) __interrupt(2) {
    printf("external_1\n");
}

void timer_1(void) __interrupt(3) {
    printf("timer_1\n");
}

void serial(void) __interrupt(4) {
    printf("serial\n");
}

void timer_2(void) __interrupt(5) {
    printf("timer_2\n");
}

void init(void) {
    gpio_init();
    gctrl_init();
    kbc_init();
    pmc_init();
    kbscan_init();
    pwm_init();
    smbus_init();

    //TODO: INTC, PECI

    // PECI information can be found here: https://www.intel.com/content/dam/www/public/us/en/documents/design-guides/core-i7-lga-2011-guide.pdf
}

void ac_adapter() {
    static struct Gpio __code ACIN_N = GPIO(B, 6);
    static struct Gpio __code LED_ACIN = GPIO(C, 7);

    static bool last = false;

    // Check if the adapter line goes low
    bool new = gpio_get(&ACIN_N);
    // Set ACIN LED
    gpio_set(&LED_ACIN, !new);

    // If there has been a change, print
    if (new != last) {
        printf("Power adapter ");
        if (new) {
            printf("unplugged\n");
        } else {
            printf("plugged in\n");
        }
    }

    last = new;
}

void power_button() {
    static struct Gpio __code PCH_DPWROK_EC =   GPIO(A, 3);
    static struct Gpio __code PCH_PWROK_EC =    GPIO(A, 4);
    static struct Gpio __code LED_PWR =         GPIO(A, 7);
    static struct Gpio __code ALL_SYS_PWRGD =   GPIO(C, 0);
    static struct Gpio __code PM_PWROK =        GPIO(C, 6);
    static struct Gpio __code PWR_SW_N =        GPIO(D, 0);
    static struct Gpio __code BUF_PLT_RST_N =   GPIO(D, 2);
    static struct Gpio __code PWR_BTN_N =       GPIO(D, 5);
    static struct Gpio __code SUSWARN_N =       GPIO(D, 7);
    static struct Gpio __code EC_EN =           GPIO(E, 1);
    static struct Gpio __code VA_EC_EN =        GPIO(E, 3);
    static struct Gpio __code DD_ON =           GPIO(E, 4);
    static struct Gpio __code EC_RSMRST_N =     GPIO(E, 5);
    static struct Gpio __code AC_PRESENT =      GPIO(E, 7);
    static struct Gpio __code SUSC_N_PCH =      GPIO(H, 1);
    static struct Gpio __code VR_ON =           GPIO(H, 4);
    static struct Gpio __code SUSB_N_PCH =      GPIO(H, 6);
    static struct Gpio __code SLP_SUS_N =       GPIO(I, 2);
    static struct Gpio __code SUS_PWR_ACK =     GPIO(J, 0);

    static bool power = false;

    // Check if the power switch goes low
    static bool last = true;
    bool new = gpio_get(&PWR_SW_N);
    if (!new && last) {
        // Ensure press is not spurious
        delay_ms(100);
        if (gpio_get(&PWR_SW_N) != new) {
            printf("Spurious press\n");
            return;
        }

        printf("Power switch press\n");

        power = !power;

        if (power) {
            printf("Enabling S5 power\n");

            // We assume that VCCRTC has already been stable, RTCRST# is
            // already set, and VCCDSW_3P3 is stable

            // Enable battery charger - also provides correct power levels for
            // system boot sourced from the AC adapter
            //battery_charger_enable();

            // Make sure VCCDSW is stable for at least 10 ms (tPCH02)
            delay_ms(10 + 5);

            // Assert DSW_PWROK
            printf("PCH_DPWROK_EC: %d\n", power);
            gpio_set(&PCH_DPWROK_EC, power);

            // Wait for SLP_SUS# (tPCH32)
            delay_ms(95);
            for (;;) {
                bool value = gpio_get(&SLP_SUS_N);
                printf("SLP_SUS_N: %d\n", value);
                if (value) break;
                delay_ms(1);
            }

            // Enable VCCPRIM_* planes - must be enabled prior to USB power
            // in order to avoid leakage
            printf("VA_EC_EN: %d\n", power);
            gpio_set(&VA_EC_EN, power);

            // Make sure VCCPRIM_* is stable for at least 10 ms (tPCH03)
            delay_ms(10 + 5);

            // Enable VDD5
            printf("DD_ON: %d\n", power);
            gpio_set(&DD_ON, power);

            // Assert RSMRST#
            printf("EC_RSMRST_N: %d\n", power);
            gpio_set(&EC_RSMRST_N, power);

            // Allow processor to control SUSB# and SUSC#
            printf("EC_EN: %d\n", power);
            gpio_set(&EC_EN, power);

            // Assert SUS_ACK#
            printf("SUS_PWR_ACK: %d\n", power);
            gpio_set(&SUS_PWR_ACK, power);

            // printf("VR_ON: %d\n", power);
            // gpio_set(&VR_ON, power);
        }
    }

    // Set PWR_BTN line!
    gpio_set(&PWR_BTN_N, new);

    if (!new && last) {
        if (power) {
            printf("Enabling S0 power\n");

            // Wait for ALL_SYS_PWRGD
            for (;;) {
                bool value = gpio_get(&ALL_SYS_PWRGD);
                printf("ALL_SYS_PWRGD: %d\n", value);
                if (value) break;
                delay_ms(1);
            }

            // Assert VR_ON
            printf("VR_ON: %d\n", power);
            gpio_set(&VR_ON, power);

            // Assert PM_PWEROK, PCH_PWROK will be asserted when H_VR_READY is
            printf("PM_PWROK: %d\n", power);
            gpio_set(&PM_PWROK, power);

            // OEM defined delay from ALL_SYS_PWRGD to SYS_PWROK - TODO
            delay_ms(10);

            // Assert PCH_PWEROK_EC, SYS_PWEROK will be asserted
            printf("PCH_PWROK_EC: %d\n", power);
            gpio_set(&PCH_PWROK_EC, power);

            // Wait for PLT_RST#
            for (;;) {
                bool value = gpio_get(&BUF_PLT_RST_N);
                printf("BUF_PLT_RST_N: %d\n", value);
                if (value) break;
                delay_ms(1);
            }
        } else {
            printf("Disabling power\n");

            // De-assert SUS_ACK#
            printf("SUS_PWR_ACK: %d\n", power);
            gpio_set(&SUS_PWR_ACK, power);

            // De-assert PCH_PWEROK_EC, SYS_PWEROK will be de-asserted
            printf("PCH_PWROK_EC: %d\n", power);
            gpio_set(&PCH_PWROK_EC, power);

            // De-assert PM_PWEROK, PCH_PWROK will be de-asserted
            printf("PM_PWROK: %d\n", power);
            gpio_set(&PM_PWROK, power);

            // De-assert VR_ON
            printf("VR_ON: %d\n", power);
            gpio_set(&VR_ON, power);

            // Block processor from controlling SUSB# and SUSC#
            printf("EC_EN: %d\n", power);
            gpio_set(&EC_EN, power);

            // De-assert RSMRST#
            printf("EC_RSMRST_N: %d\n", power);
            gpio_set(&EC_RSMRST_N, power);

            // Disable VDD5
            printf("DD_ON: %d\n", power);
            gpio_set(&DD_ON, power);

            // Wait a minimum of 400 ns (tPCH12)
            delay_ms(1);

            // Disable VCCPRIM_* planes
            printf("VA_EC_EN: %d\n", power);
            gpio_set(&VA_EC_EN, power);

            // De-assert DSW_PWROK
            printf("PCH_DPWROK_EC: %d\n", power);
            gpio_set(&PCH_DPWROK_EC, power);

            // Wait a minimum of 400 ns (tPCH14)
            delay_ms(1);

            // Disable battery charger
            //battery_charger_disable();
        }

        printf("LED_PWR: %d\n", power);
        gpio_set(&LED_PWR, power);
    } else if (new && !last) {
        printf("Power switch release\n");

        printf("SUSWARN_N: %d\n", gpio_get(&SUSWARN_N));
        printf("SUSC_N_PCH: %d\n", gpio_get(&SUSC_N_PCH));
        printf("SUSB_N_PCH: %d\n", gpio_get(&SUSB_N_PCH));
        printf("ALL_SYS_PWRGD: %d\n", gpio_get(&ALL_SYS_PWRGD));
        printf("BUF_PLT_RST_N: %d\n", gpio_get(&BUF_PLT_RST_N));

        //battery_debug();
    }

    last = new;
}

void touchpad_event(struct Ps2 * ps2) {
    //TODO
}

struct Gpio __code LED_SSD_N = GPIO(G, 6);
struct Gpio __code LED_AIRPLANE_N = GPIO(G, 6);

void main(void) {
    init();

    printf("\n");

    static struct Gpio __code LED_BAT_CHG =     GPIO(A, 5);
    static struct Gpio __code LED_BAT_FULL =    GPIO(A, 6);
    static struct Gpio __code SMI_N =           GPIO(D, 3);
    static struct Gpio __code SCI_N =           GPIO(D, 4);
    static struct Gpio __code SWI_N =           GPIO(E, 0);
    static struct Gpio __code SB_KBCRST_N =     GPIO(E, 6);
    static struct Gpio __code PM_CLKRUN_N =     GPIO(H, 0);
    static struct Gpio __code BKL_EN =          GPIO(H, 2);

    // Set the battery full LED (to know our firmware is loading)
    gpio_set(&LED_BAT_CHG, true);

    gpio_debug();

    //battery_debug();

    // Allow CPU to boot
    gpio_set(&SB_KBCRST_N, true);
    // Allow backlight to be turned on
    gpio_set(&BKL_EN, true);
    // Assert SMI#, SCI#, and SWI#
    gpio_set(&SCI_N, true);
    gpio_set(&SMI_N, true);
    gpio_set(&SWI_N, true);

    // Set the battery full LED (to know our firmware is loaded)
    gpio_set(&LED_BAT_FULL, true);
    printf("Hello from System76 EC for %s!\n", xstr(__BOARD__));

    for(;;) {
        ac_adapter();
        power_button();
        kbscan_event();
        touchpad_event(&PS2_3);
        kbc_event(&KBC);
        pmc_event(&PMC_1);
    }
}
