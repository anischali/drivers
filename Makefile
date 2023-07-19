all:
	for i in *; do if [ -e "$$i"/Makefile ]; then make -C "$$i"; fi; done
clean:
	for i in *; do if [ -e "$$i"/Makefile ]; then make -C "$$i" clean; fi; done