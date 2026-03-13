obj-m += project_driver.o

MOD_DIR      := $(PWD)
MODBUILD_DIR := $(MOD_DIR)/build

mod:
	mkdir -p $(MODBUILD_DIR)
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(MOD_DIR) MO=$(MODBUILD_DIR) modules

mod_clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(MOD_DIR) MO=$(MODBUILD_DIR) clean
	rm -rf $(MODBUILD_DIR)
