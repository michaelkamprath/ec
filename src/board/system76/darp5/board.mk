# SPDX-License-Identifier: GPL-3.0-only

EC=ite
EC_VARIANT=it8587e

# Include keyboard
KEYBOARD=15in_102

# Set keyboard LED mechanism
KBLED=darp5
CFLAGS+=-DI2C_KBLED=I2C_1

# Set battery I2C bus
CFLAGS+=-DI2C_SMBUS=I2C_0

# Set touchpad PS2 bus
CFLAGS+=-DPS2_TOUCHPAD=PS2_3

# Set smart charger parameters
CFLAGS+=\
	-DCHARGER_ADAPTER_RSENSE=10 \
	-DCHARGER_BATTERY_RSENSE=10 \
	-DCHARGER_CHARGE_CURRENT=3072 \
	-DCHARGER_CHARGE_VOLTAGE=17600 \
	-DCHARGER_INPUT_CURRENT=3420

# Add system76 common code
include src/board/system76/common/common.mk
