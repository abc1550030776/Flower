flower:=Flower

.PHONY : all $(flower) clean

all : $(flower)

$(flower) :
	$(MAKE) -C $@

clean : $(flower)clean

$(flower)clean :
	$(MAKE) -C $(flower) clean
