obj-m += blackbox_driver.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

# 輔助指令：載入並設定權限
load:
	sudo insmod blackbox_driver.ko
	# 使用 sed 或更精確的 awk 抓取數字
	MAJOR=$$(dmesg | grep "Blackbox: Module loaded" | tail -1 | sed 's/.*major \([0-9]*\).*/\1/'); \
	sudo mknod /dev/blackbox c $$MAJOR 0
	sudo chmod 666 /dev/blackbox

unload:
	sudo rmmod blackbox_driver
	sudo rm /dev/blackbox
