# spec_xga
HQx4 upscaled spectrum screen drawing
simplified HQx4 upscaler to handle Sinclair graphics optimally. Intention is to have a smooth 1024x768 screen without borders from orignal 256x192
(320x240 with borders).
Now it is possible to compare several screenshots (screens/*) upscaled with simple 4x4 pixel multiply and hqx4 algorithm, drawn into SDL2 canvas.
# example launch
```
build/vgasdl screens/Arkanoid.scr
```
use 'C' to switch between algorithms and 'esc' to exit.
