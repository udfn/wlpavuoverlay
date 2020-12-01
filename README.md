# WlPaVUOverlay
![](screenshot.png)

WlPaVUOverlay is a simple utility for quickly changing Pulseaudio volume levels.
Made exclusively for Wayland compositors. Preferably those that support `wlr-layer-shell`.
## Warning!
I made this to learn Wayland programming, so the code is horrible in many ways. You probably shouldn't use this as a learning resource, unless it's to learn how not to do things.
## Usage
Just run it and it'll use layer-shell to show itself over everything. If layer-shell isn't available it falls back to xdg-shell.
There are a couple arguments..
* `wlpavuoverlay bla` - You can force xdg-shell by simply adding any argument. Except...
* `wlpavuoverlay dim` - "Fullscreen" mode. This makes mouse usage easier if another surface has locked the pointer.

There are currently hardcoded keyboard commands..
* **j k** - move down / up
* **h l** - adjust selected volume lower / higher, hold shift for a larger adjustment
* **m** - toggle mute on selected
## Compiling
This is a simple Meson project, so the good old `meson build` `ninja -C build` is a good start.
### Dependencies
Many, but you probably already have most of these...
* A working C compiler
* Epoxy
* Wayland (obviously)
* Cairo (RIP)
* EGL & GL
* Xkbcommon
* Pulseaudio
## TODO (in no particular order and will probably not be done)
- [ ] Handle recording devices & streams
- [ ] Clean up the code
- [ ] Touch input support
- [ ] Better interface toolkit
- [ ] Support volumes over 100%
- [ ] Adjust individual channels
- [ ] Rewrite in whatever language is hot this month
- [ ] Changing stream sinks & sources and defaults
- [ ] Keyboard input - Partially done
- [ ] Proper multiseat support
- [ ] Fractional scaling (how? Wayland only allows integer scales for buffers)
- [x] Generate the protocol headers & code during build
- [ ] Audio level meters, like `pavucontrol`
- [ ] PipeWire (or Jack?) support
## License?
Unlicense. See the file `UNLICENSE` for the full text, or https://unlicense.org

The XML protocol files in the `protocol` directory have different licenses though.
