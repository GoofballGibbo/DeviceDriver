MOD_DIR       := $(PWD)/module
MODBUILD_DIR  := $(MOD_DIR)/build
FW_DIR        := $(PWD)/firmware
FWBUILD_DIR   := $(FW_DIR)/build
PICO_SDK_PATH := $(FW_DIR)/pico-sdk

all: mod fw

mod:
	mkdir -p $(MODBUILD_DIR)
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(MOD_DIR) MO=$(MODBUILD_DIR) modules

fw:
	mkdir -p $(FWBUILD_DIR)
	cmake -B $(FWBUILD_DIR) -S $(FW_DIR)
	$(MAKE) -C $(FWBUILD_DIR)

clean: mod_clean fw_clean

mod_clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(MOD_DIR) MO=$(MODBUILD_DIR) clean
	rm -rf $(MODBUILD_DIR)

fw_clean:
	rm -rf $(FW_DIR)/generated
	rm -rf $(FWBUILD_DIR)
