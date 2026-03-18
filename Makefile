obj-m += project_driver.o

BUILD_DIR   := $(PWD)/build
SRC_DIR     := $(PWD)
FW_DIR      := $(PWD)/firmware
FWBUILD_DIR := $(FW_DIR)/build

all: mod fw

mod:
	mkdir -p $(BUILD_DIR)
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(SRC_DIR) MO=$(BUILD_DIR) modules

fw:
	mkdir -p $(FWBUILD_DIR)
	cmake -B $(FWBUILD_DIR) -S $(FW_DIR)
	$(MAKE) -C $(FWBUILD_DIR)

clean: mod_clean fw_clean

mod_clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(SRC_DIR) MO=$(BUILD_DIR) clean
	rm -rf $(BUILD_DIR)

fw_clean:
	rm -rf $(FWBUILD_DIR)
