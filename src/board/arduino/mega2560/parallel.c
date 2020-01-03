// High resolution pinout can be found here:
// https://osoyoo.com/wp-content/uploads/2017/08/arduino_mega_2560_pinout.png

#include <stdint.h>
#include <stdio.h>
#include <board/cpu.h>
#include <util/delay.h>

#include <arch/gpio.h>
#include <arch/uart.h>

// Mapping of 24-pin ribbon cable to parallel pins. See schematic
#define PINS \
    /* Data (KSO0 - KSO7) - bi-directional */ \
    PIN(d0, 1) \
    PIN(d1, 2) \
    PIN(d2, 3) \
    PIN(d3, 7) \
    PIN(d4, 9) \
    PIN(d5, 10) \
    PIN(d6, 13) \
    PIN(d7, 16) \
    /* Wait# (KSO9) - input */ \
    /*  low to indicate cycle may begin, high to indicate cycle may end */ \
    PIN(wait_n, 18) \
    /* Write# (KSI0) - output */ \
    /*  low to indicate write cycle, high to indicate read cycle */ \
    PIN(write_n, 4) \
    /* DataStrobe# (KSI1) - output */ \
    /*  low indicates a data cycle */ \
    PIN(data_n, 5) \
    /* Reset# (KSI2) - output */ \
    /*  low requests device reset */ \
    PIN(reset_n, 6) \
    /* AddressStrobe# (KSI3) - output */ \
    /*  low indicates an address cycle */ \
    PIN(addr_n, 8) \
    /* Strap0 (KSI4) */ \
    /*  1K-Ohm pull-down resistor */ \
    PIN(strap_0, 11) \
    /* Strap1 (KSI5) */ \
    /*  1K-Ohm pull-down resistor */ \
    PIN(strap_1, 12)

#define DATA_BITS \
    DATA_BIT(0) \
    DATA_BIT(1) \
    DATA_BIT(2) \
    DATA_BIT(3) \
    DATA_BIT(4) \
    DATA_BIT(5) \
    DATA_BIT(6) \
    DATA_BIT(7)

// Mapping of 24-pin ribbon cable to GPIOs
static struct Gpio GPIOS[24] = {
    GPIO(L, 4), GPIO(L, 5),
    GPIO(L, 6), GPIO(L, 7),
    GPIO(G, 0), GPIO(G, 1),
    GPIO(G, 2), GPIO(D, 7),
    GPIO(C, 0), GPIO(C, 1),
    GPIO(C, 2), GPIO(C, 3),
    GPIO(C, 4), GPIO(C, 5),
    GPIO(C, 6), GPIO(C, 7),
    GPIO(A, 7), GPIO(A, 6),
    GPIO(A, 5), GPIO(A, 4),
    GPIO(A, 3), GPIO(A, 2),
    GPIO(A, 1), GPIO(A, 0),
};

// Parallel struct definition
// See http://efplus.com/techref/io/parallel/1284/eppmode.htm
struct Parallel {
    #define PIN(N, P) struct Gpio * N;
    PINS
    #undef PIN
};

// Parallel struct instance
static struct Parallel PORT = {
    #define PIN(N, P) .N = &GPIOS[P - 1],
    PINS
    #undef PIN
};

// Set port to all high-impedance inputs
void parallel_hiz(struct Parallel * port) {
    #define PIN(N, P) \
        gpio_set_dir(port->N, false); \
        gpio_set(port->N, false);
    PINS
    #undef PIN
}

// Set port to initial state required before being able to perform cycles
void parallel_reset(struct Parallel * port) {
    parallel_hiz(port);

    // Set reset line low
    gpio_set_dir(port->reset_n, true);

    // Wait 1 microsecond
    _delay_us(1);
    
    // Make sure strobes are high outputs
    gpio_set(port->data_n, true);
    gpio_set(port->addr_n, true);
    gpio_set_dir(port->data_n, true);
    gpio_set_dir(port->addr_n, true);

    // Set write line high output
    gpio_set(port->write_n, true);
    gpio_set_dir(port->write_n, true);

    // Pull up wait line
    gpio_set(port->wait_n, true);

    // Pull up data lines
    #define DATA_BIT(B) gpio_set(port->d ## B, true);
    DATA_BITS
    #undef DATA_BIT

    //TODO: something with straps
    
    // Wait 1 microsecond
    _delay_us(1);

    // Set reset line high, ending reset
    gpio_set(port->reset_n, true);
}

//TODO: timeout
int parallel_transaction(struct Parallel * port, uint8_t * data, int length, bool read, bool addr) {
    if (!read) {
        // Set write line low
        gpio_set(port->write_n, false);

        // Set data direction out
        #define DATA_BIT(B) gpio_set_dir(port->d ## B, true);
        DATA_BITS
        #undef DATA_BIT
    }

    int i;
    uint8_t byte;
    for (i = 0; i < length; i++) {
        // Wait for wait line to be low
        while (gpio_get(port->wait_n)) {}
        
        if (!read) {
            // Set data low where necessary
            byte = data[i];
            #define DATA_BIT(B) if (!(byte & (1 << B))) gpio_set(port->d ## B, false);
            DATA_BITS
            #undef DATA_BIT
        }

        if (addr) {
            // Set address strobe low
            gpio_set(port->addr_n, false);
        } else {
            // Set data strobe low
            gpio_set(port->data_n, false);
        }

        // Wait for wait line to be high
        while (!gpio_get(port->wait_n)) {}

        if (read) {
            byte = 0;
            #define DATA_BIT(B) if (gpio_get(port->d ## B)) byte |= (1 << B);
            DATA_BITS
            #undef DATA_BIT
            data[i] = byte;
        }

        if (addr) {
            // Set address strobe high
            gpio_set(port->addr_n, true);
        } else {
            // Set data strobe high
            gpio_set(port->data_n, true);
        }

        if (!read) {
            // Set data high where necessary
            #define DATA_BIT(B) if (!(byte & (1 << B))) gpio_set(port->d ## B, true);
            DATA_BITS
            #undef DATA_BIT
        }
    }

    if (!read) {
        // Set data direction in
        #define DATA_BIT(B) gpio_set_dir(port->d ## B, false);
        DATA_BITS
        #undef DATA_BIT

        // Set write line high
        gpio_set(port->write_n, true);
    }

    return i;
}

#define parallel_get_address(P, D, L) parallel_transaction(P, D, L, true, true)
#define parallel_set_address(P, D, L) parallel_transaction(P, D, L, false, true)
#define parallel_read(P, D, L) parallel_transaction(P, D, L, true, false)
#define parallel_write(P, D, L) parallel_transaction(P, D, L, false, false)

static uint8_t ADDRESS_INDAR1 = 0x05;
static uint8_t ADDRESS_INDDR = 0x08;

static uint8_t ZERO = 0x00;
static uint8_t SPI_ENABLE = 0xFE;
static uint8_t SPI_DATA = 0xFD;

// Disable chip
int parallel_spi_reset(struct Parallel *port) {
    int res;

    res = parallel_set_address(port, &ADDRESS_INDAR1, 1);
    if (res < 0) return res;
    
    res = parallel_write(port, &SPI_ENABLE, 1);
    if (res < 0) return res;

    res = parallel_set_address(port, &ADDRESS_INDDR, 1);
    if (res < 0) return res;

    return parallel_write(port, &ZERO, 1);
}

// Enable chip and read or write data
int parallel_spi_transaction(struct Parallel *port, uint8_t * data, int length, bool read) {
    int res;

    res = parallel_set_address(port, &ADDRESS_INDAR1, 1);
    if (res < 0) return res;
    
    res = parallel_write(port, &SPI_DATA, 1);
    if (res < 0) return res;

    res = parallel_set_address(port, &ADDRESS_INDDR, 1);
    if (res < 0) return res;

    return parallel_transaction(port, data, length, read, false);
}

#define parallel_spi_read(P, D, L) parallel_spi_transaction(P, D, L, true)
#define parallel_spi_write(P, D, L) parallel_spi_transaction(P, D, L, false)

// "Hardware" accelerated SPI programming, requires ECINDARs to be set
int parallel_spi_program(struct Parallel * port, uint8_t * data, int length, bool initialized) {
    static uint8_t aai[6] = { 0xAD, 0, 0, 0, 0, 0 };
    int res;
    int i;
    uint8_t status;

    for(i = 0; (i + 1) < length; i+=2) {
        // Disable chip to begin command
        res = parallel_spi_reset(port);
        if (res < 0) return res;

        if (!initialized) {
            // If not initialized, the start address must be sent
            aai[1] = 0;
            aai[2] = 0;
            aai[3] = 0;

            aai[4] = data[i];
            aai[5] = data[i + 1];

            res = parallel_spi_write(port, aai, 6);
            if (res < 0) return res;

            initialized = true;
        } else {
            aai[1] = data[i];
            aai[2] = data[i + 1];

            res = parallel_spi_write(port, aai, 3);
            if (res < 0) return res;
        }

        // Wait for SPI busy flag to clear
        for (;;) {
            // Disable chip to begin command
            res = parallel_spi_reset(port);
            if (res < 0) return res;

            status = 0x05;
            res = parallel_spi_write(port, &status, 1);
            if (res < 0) return res;

            res = parallel_spi_read(port, &status, 1);
            if (res < 0) return res;

            if (!(status & 1)) break;
        }
    }

    return i;
}

int serial_transaction(uint8_t * data, int length, bool read) {
    int i;
    for (i = 0; i < length; i++) {
        if (read) {
            data[i] = (uint8_t)uart_read(uart_stdio);
        } else {
            uart_write(uart_stdio, (unsigned char)data[i]);
        }
    }

    return i;
}

#define serial_read(D, L) serial_transaction(D, L, true)
#define serial_write(D, L) serial_transaction(D, L, false)

int parallel_main(void) {
    int res = 0;

    struct Parallel * port = &PORT;
    parallel_reset(port);

    static uint8_t data[128];
    char command;
    int length;
    int i;
    uint8_t address;
    bool set_address = false;
    bool program_aai = false;
    for (;;) {
        // Read command and length
        res = serial_read(data, 2);
        if (res < 0) goto err;
        // Command is a character
        command = (char)data[0];

        // Special address prefix
        if (command == 'A') {
            // Prepare to set address on next parallel cycle
            address = data[1];
            set_address = true;
            continue;
        } else if (command != 'P') {
            // End accelerated programming
            program_aai = false;
        }

        // Length is received data + 1
        length = ((int)data[1]) + 1;
        // Truncate length to size of data
        if (length > sizeof(data)) length = sizeof(data);

        switch (command) {
            // Buffer size
            case 'B':
                // Fill buffer size - 1
                for (i = 0; i < length; i++) {
                    if (i == 0) {
                        data[i] = (uint8_t)(sizeof(data) - 1);
                    } else {
                        data[i] = 0;
                    }
                }
                
                // Write data to serial
                res = serial_write(data, length);
                if (res < 0) goto err;

                break;

            // Echo
            case 'E':
                // Read data from serial
                res = serial_read(data, length);
                if (res < 0) goto err;

                // Write data to serial
                res = serial_write(data, length);
                if (res < 0) goto err;

                break;

            // Read data
            case 'R':
                // Update parallel address if necessary
                if (set_address) {
                    res = parallel_set_address(port, &address, 1);
                    if (res < 0) goto err;
                    set_address = false;
                }

                // Read data from parallel
                res = parallel_read(port, data, length);
                if (res < 0) goto err;

                // Write data to serial
                res = serial_write(data, length);
                if (res < 0) goto err;

                break;

            // Accelerated program function
            case 'P':
                // Read data from serial
                res = serial_read(data, length);
                if (res < 0) goto err;

                // Run accelerated programming function
                res = parallel_spi_program(port, data, length, program_aai);
                if (res < 0) goto err;
                program_aai = true;

                // Send ACK of data length
                data[0] = (uint8_t)(length - 1);
                res = serial_write(data, 1);
                if (res < 0) goto err;

                break;

            // Write data
            case 'W':
                // Read data from serial
                res = serial_read(data, length);
                if (res < 0) goto err;

                // Update parallel address if necessary
                if (set_address) {
                    res = parallel_set_address(port, &address, 1);
                    if (res < 0) goto err;
                    set_address = false;
                }

                // Write data to parallel
                res = parallel_write(port, data, length);
                if (res < 0) goto err;

                // Send ACK of data length
                data[0] = (uint8_t)(length - 1);
                res = serial_write(data, 1);
                if (res < 0) goto err;

                break;
        }
    }

err:
    parallel_hiz(port);

    return res;
}
