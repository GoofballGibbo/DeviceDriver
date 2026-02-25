obj-m += project_driver.o

BUILD_DIR := $(PWD)/build

all:
	mkdir -p $(BUILD_DIR)
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) MO=$(BUILD_DIR) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) MO=$(BUILD_DIR) clean
	rm -rf $(BUILD_DIR)
