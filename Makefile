
MAKE = make

.PHONY: all
all:
	$(MAKE) -j -C curl all
	$(MAKE) -j -C expat all
	$(MAKE) -j -C FLIR_Lepton all
	$(MAKE) -j -C mbed-gr-libs all
	$(MAKE) -j -C mbed-os all
	$(MAKE) -j -C opencv-lib all
	$(MAKE) -j -C utilities all
	$(MAKE) -j -C zlib all
	$(MAKE) -j -C zxing all
	$(MAKE) -j -C HoikuCam all

.PHONY: clean
clean:
	$(MAKE) -C curl clean
	$(MAKE) -C expat clean
	$(MAKE) -C FLIR_Lepton clean
	$(MAKE) -C mbed-gr-libs clean
	$(MAKE) -C mbed-os clean
	$(MAKE) -C opencv-lib clean
	$(MAKE) -C utilities clean
	$(MAKE) -C zlib clean
	$(MAKE) -C zxing clean
	$(MAKE) -C HoikuCam clean
