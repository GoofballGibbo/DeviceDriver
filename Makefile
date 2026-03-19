MOD_DIR       := $(PWD)/module
MODBUILD_DIR  := $(MOD_DIR)/build
FW_DIR        := $(PWD)/firmware
FWBUILD_DIR   := $(FW_DIR)/build
USR_DIR       := $(PWD)/userapp
USRBUILD_DIR  := $(USR_DIR)/build
PICO_SDK_PATH := $(FW_DIR)/pico-sdk

all: mod fw user

mod:
	mkdir -p $(MODBUILD_DIR)
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(MOD_DIR) MO=$(MODBUILD_DIR) modules

fw:
	mkdir -p $(FWBUILD_DIR)
	cmake -B $(FWBUILD_DIR) -S $(FW_DIR)
	$(MAKE) -C $(FWBUILD_DIR)

user:
	mkdir -p $(USRBUILD_DIR)
	$(CC) $(USR_DIR)/typing_test.c -o $(USRBUILD_DIR)/typing_test

clean: mod_clean fw_clean

mod_clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(MOD_DIR) MO=$(MODBUILD_DIR) clean
	rm -rf $(MODBUILD_DIR)

fw_clean:
	rm -rf $(FW_DIR)/generated
	rm -rf $(FWBUILD_DIR)

usr_clean:
	rm -rf $(USRBUILD_DIR)

install:
	sudo cp $(PWD)/udev/* /etc/udev/rules.d/
