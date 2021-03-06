DIR_CONFIG   = ./lib/config
DIR_EPD      = ./lib/epd
DIR_FONTS    = ./lib/fonts
DIR_JSON     = ./lib/json
DIR_GUI      = ./lib/gui
DIR_SRC      = ./src
DIR_BIN      = ./bin

TARGET = kotetsu
CC = gcc
MSG = -g -O -ffunction-sections -fdata-sections -Wall -Wno-unused-variable
CFLAGS += $(MSG)

OBJ_C = $(wildcard ${DIR_EPD}/*.c ${DIR_GUI}/*.c ${DIR_SRC}/*.c ${DIR_FONTS}/*.c  ${DIR_JSON}/*.c)
OBJ_O = $(patsubst %.c,${DIR_BIN}/%.o,$(notdir ${OBJ_C}))
RPI_DEV_C = $(wildcard $(DIR_BIN)/dev_hardware_SPI.o $(DIR_BIN)/RPI_sysfs_gpio.o $(DIR_BIN)/DEV_Config.o )
JETSON_DEV_C = $(wildcard $(DIR_BIN)/sysfs_software_spi.o $(DIR_BIN)/sysfs_gpio.o $(DIR_BIN)/DEV_Config.o )

# DEBUG = -D DEBUG

USELIB_RPI = USE_BCM2835_LIB
# USELIB_RPI = USE_WIRINGPI_LIB
# USELIB_RPI = USE_DEV_LIB

LIB_RPI=-Wl,--gc-sections,-lcurl
ifeq ($(USELIB_RPI), USE_BCM2835_LIB)
	LIB_RPI += -lbcm2835 -lm
else ifeq ($(USELIB_RPI), USE_WIRINGPI_LIB)
	LIB_RPI += -lwiringPi -lm
else ifeq ($(USELIB_RPI), USE_DEV_LIB)
	LIB_RPI += -lm
endif

DEBUG_RPI = -D $(USELIB_RPI) -D RPI

USELIB_JETSONI = USE_DEV_LIB
# USELIB_JETSONI = USE_HARDWARE_LIB
ifeq ($(USELIB_JETSONI), USE_DEV_LIB)
	LIB_JETSONI = -lm
else ifeq ($(USELIB_JETSONI), USE_HARDWARE_LIB)
	LIB_JETSONI = -lm
endif

DEBUG_JETSONI = -D $(USELIB_JETSONI) -D JETSON

.PHONY : RPI JETSON clean

RPI: RPI_DEV RPI_epd

JETSON: JETSON_DEV JETSON_epd

RPI_epd:${OBJ_O}
	$(CC) $(CFLAGS) -D RPI $(OBJ_O) $(RPI_DEV_C) -o $(TARGET) $(LIB_RPI) $(DEBUG)

JETSON_epd:${OBJ_O}
	echo $(@)
	$(CC) $(CFLAGS) $(OBJ_O) $(JETSON_DEV_C) -o $(TARGET) $(LIB_JETSONI) $(DEBUG)

$(shell mkdir -p $(DIR_BIN))

${DIR_BIN}/%.o:$(DIR_SRC)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ -I $(DIR_CONFIG) -I $(DIR_GUI) -I $(DIR_EPD) -I $(DIR_JSON) $(DEBUG)

${DIR_BIN}/%.o:$(DIR_EPD)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ -I $(DIR_CONFIG) $(DEBUG)

${DIR_BIN}/%.o:$(DIR_FONTS)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ $(DEBUG)

${DIR_BIN}/%.o:$(DIR_JSON)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ $(DEBUG)

${DIR_BIN}/%.o:$(DIR_GUI)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ -I $(DIR_CONFIG) $(DEBUG)

RPI_DEV:
	$(CC) $(CFLAGS) $(DEBUG_RPI) -c  $(DIR_CONFIG)/dev_hardware_SPI.c -o $(DIR_BIN)/dev_hardware_SPI.o $(LIB_RPI) $(DEBUG)
	$(CC) $(CFLAGS) $(DEBUG_RPI) -c  $(DIR_CONFIG)/RPI_sysfs_gpio.c -o $(DIR_BIN)/RPI_sysfs_gpio.o $(LIB_RPI) $(DEBUG)
	$(CC) $(CFLAGS) $(DEBUG_RPI) -c  $(DIR_CONFIG)/DEV_Config.c -o $(DIR_BIN)/DEV_Config.o $(LIB_RPI) $(DEBUG)

JETSON_DEV:
	$(CC) $(CFLAGS) $(DEBUG_JETSONI) -c  $(DIR_CONFIG)/sysfs_software_spi.c -o $(DIR_BIN)/sysfs_software_spi.o $(LIB_JETSONI) $(DEBUG)
	$(CC) $(CFLAGS) $(DEBUG_JETSONI) -c  $(DIR_CONFIG)/sysfs_gpio.c -o $(DIR_BIN)/sysfs_gpio.o $(LIB_JETSONI) $(DEBUG)
	$(CC) $(CFLAGS) $(DEBUG_JETSONI) -c  $(DIR_CONFIG)/DEV_Config.c -o $(DIR_BIN)/DEV_Config.o $(LIB_JETSONI)  $(DEBUG)

clean :
	rm $(DIR_BIN)/*.*
	rm $(TARGET)

