# Yulegen
🎄 Light up your holidays with GenAI magic! 🌟 

Add a festive twist to your Christmas tree with randomly generated GenAI images on a 32x32 LED Matrix. This cheerful display, powered by [Raspberry Pi 4](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/), [Adafruit RGB Matrix HAT](https://www.adafruit.com/product/1484),  [32x32 LED Matrix](https://www.adafruit.com/product/1484) and creatively framed with Lego blocks. Get ready for a bright and merry season! 🎅✨"

[![Matrix Setup](https://img.youtube.com/vi/IKnOzuWmZjo/0.jpg)](https://youtube.com/shorts/7bKkrBihFzk "Demo")

## Hardware
[Tutorial](https://learn.adafruit.com/adafruit-rgb-matrix-plus-real-time-clock-hat-for-raspberry-pi/overview) has all the instructions to setup LED Matrix with HAT. [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) provides all the libraries and low level functions.

#### My setup
- Both Raspberry Pi and Matrix is driven by [Aukey Wall Chargers](https://www.aukey.com/collections/chargers) each powered from a USB port. The Aukey chargers drive sufficient current and voltage for the matrix. Always do the math and add some buffer to avoid flickering.
- I did not connect the terminal block in HAT to the matrix. This was because my HAT's terminal block was busted and wouldn't drive voltate to the matrix. Instead, I directly connected the spade connector from the matrix to the USB charger. The HAT was driven by whatever power was provided by the Pi which was sufficient.
![Power Setup](https://onedrive.live.com/embed?resid=F33E492FF42DEBDD%21439439&authkey=%21AE9fu5LviCxiSBc&width=660)
- I soldered a jumper wire from GPIO4 and GPIO18 for smooth rendering. This comes at the cost of disabling the audio drivers in the Pi. I did not use RTC(figured it wasn't needed for my 32x32).
- Hide all the nasty stuff behind the tree and have a nice Lego facade 😀
![Appearance](https://onedrive.live.com/embed?resid=F33E492FF42DEBDD%21439438&authkey=%21ALeB65lcJLvBZ5Y&width=660)

## Software
- The display starts with some bootstrapped images`--bootstrap-imgs-path`(also genAi). However, these images have been prompt engineered to perfection.
- At defined speed, `--genimgs-per-hour`, it downloads images from openAi [Image generation Apis](https://platform.openai.com/docs/guides/images/image-generation?context=node). I wanted to run a model locally in the Pi, but as of 2023, this is not a good idea.
- OpenAi api key is injected as environment variable. Can also be provided with `--openai-api-key`
- The generated images are convereted and scaled using [ImageMagick](https://imagemagick.org/) and double buffered to the matrix for display. I want to add some animation, but didn't happen.
- All code is in `yulegen.cpp`. The daemon can run as systemd service starting at defined time in `yulegen-start.timer` and stopping at `yulegen-stop.timer`. Frame rate can be controlled with `--animation-duration-ms`.

### Pre-Installation
```bash
# for rendering and scaling
apt install libgraphicsmagick++-dev libwebp-dev
# for httplib to handshake tls
apt install libssl-dev
```
### Installation
```bash
make
sudo make install

# cleanup
make clean
sudo make uninstall
```