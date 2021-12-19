NAME=dbpciaddr
MAKE=make

obj-m += $(NAME).o
$(NAME)-objs := _$(NAME).o

all:
	$(MAKE) -j 8 -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

load:
	insmod $(NAME).ko

unload:
	rmmod  $(NAME) -f

clean:
	$(MAKE) -j 8 -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean