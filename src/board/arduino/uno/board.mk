# SPDX-License-Identifier: GPL-3.0-only

EC=atmega
EC_VARIANT=atmega328p

PORT=$(wildcard /dev/ttyACM*)
CONSOLE_BAUD=1000000
CFLAGS+=-D__CONSOLE_BAUD__=$(CONSOLE_BAUD)

console:
	sudo tio -b $(CONSOLE_BAUD) $(PORT)

flash: $(BUILD)/ec.ihx
	sudo avrdude -v -v -p $(EC_VARIANT) -c arduino -P $(PORT) -b 115200 -D -U flash:w:$<:i
